/**
 * @file   OrbitalController.cpp
 *
 * @date   Jan 16. 2017
 * @author Thomas Dresselhaus, Jan Unsleber
 * @copyright \n
 *  This file is part of the program Serenity.\n\n
 *  Serenity is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation, either version 3 of
 *  the License, or (at your option) any later version.\n\n
 *  Serenity is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.\n\n
 *  You should have received a copy of the GNU Lesser General
 *  Public License along with Serenity.
 *  If not, see <http://www.gnu.org/licenses/>.\n
 */
/* Include Class Header*/
#include "data/OrbitalController.h"
/* Include Serenity Internal Headers */
#include "basis/BasisController.h"
#include "data/SpinPolarizedData.h"
#include "data/matrices/CoefficientMatrix.h"
#include "data/matrices/FockMatrix.h"
#include "data/matrices/MatrixInBasis.h"
#include "integrals/OneElectronIntegralController.h"
#include "io/FormattedOutput.h"
#include "io/FormattedOutputStream.h"
#include "io/HDF5.h"
#include "misc/Timing.h"
#include "misc/WarningTracker.h"

namespace Serenity {

template<Options::SCF_MODES SCFMode>
OrbitalController<SCFMode>::OrbitalController(std::unique_ptr<CoefficientMatrix<SCFMode>> coefficients,
                                              std::shared_ptr<BasisController> basisController,
                                              std::unique_ptr<SpinPolarizedData<SCFMode, Eigen::VectorXd>> eigenvalues,
                                              std::unique_ptr<SpinPolarizedData<SCFMode, Eigen::VectorXi>> isCoreOrbital)
  : NotifyingClass<OrbitalController<SCFMode>>(),
    _coefficients(move(coefficients)),
    _basisController(basisController),
    _eigenvalues(move(eigenvalues)),
    _isCoreOrbital(move(isCoreOrbital)),
    _canOrthTh(1.0e-7),
    _linearDependent(false),
    _nZero(0) {
  auto& tmp = *_eigenvalues;
  for_spin(tmp) {
    if (!isDefinedInSameBasis(*_coefficients, *this))
      throw SerenityError("OrbitalController: Coefficients are not defined in the correct basis");
    if (tmp_spin.size() != _basisController->getNBasisFunctions())
      throw SerenityError("OrbitalController: The number of eigenvalues does not match the number of orbitals.");
  };
  _basisController->addSensitiveObject(this->_self);
}

template<Options::SCF_MODES SCFMode>
OrbitalController<SCFMode>::OrbitalController(std::unique_ptr<CoefficientMatrix<SCFMode>> coefficients,
                                              std::shared_ptr<BasisController> basisController,
                                              std::unique_ptr<SpinPolarizedData<SCFMode, Eigen::VectorXd>> eigenvalues,
                                              unsigned int nCoreElectrons)
  : OrbitalController(move(coefficients), basisController, move(eigenvalues),
                      std::make_unique<SpinPolarizedData<SCFMode, Eigen::VectorXi>>(
                          getCoreOrbitalsByEigenvalue(nCoreElectrons, *eigenvalues))) {
}

template<Options::SCF_MODES SCFMode>
OrbitalController<SCFMode>::OrbitalController(std::shared_ptr<BasisController> basisController)
  : NotifyingClass<OrbitalController<SCFMode>>(),
    _coefficients(new CoefficientMatrix<SCFMode>(basisController)),
    _basisController(basisController),
    _eigenvalues(new SpinPolarizedData<SCFMode, Eigen::VectorXd>(basisController->getNBasisFunctions())),
    _isCoreOrbital(new SpinPolarizedData<SCFMode, Eigen::VectorXi>(basisController->getNBasisFunctions())),
    _canOrthTh(1.0e-7),
    _linearDependent(false),
    _nZero(0) {
  assert(_basisController);
  _basisController->addSensitiveObject(this->_self);
}

template<Options::SCF_MODES SCFMode>
OrbitalController<SCFMode>::OrbitalController(const OrbitalController<SCFMode>& orig)
  : NotifyingClass<OrbitalController<SCFMode>>(),
    _coefficients(new CoefficientMatrix<SCFMode>(*orig._coefficients)),
    _basisController(orig._basisController),
    _eigenvalues(new SpinPolarizedData<SCFMode, Eigen::VectorXd>(*orig._eigenvalues)),
    _isCoreOrbital(new SpinPolarizedData<SCFMode, Eigen::VectorXi>(*orig._isCoreOrbital)),
    _canOrthTh(orig._canOrthTh),
    _nZero(0) {
  assert(isDefinedInSameBasis(orig, *this));
  _basisController->addSensitiveObject(this->_self);
}

template<Options::SCF_MODES SCFMode>
OrbitalController<SCFMode>::OrbitalController(std::string filePath, std::shared_ptr<BasisController> basisController,
                                              std::string id)
  : _coefficients(new CoefficientMatrix<SCFMode>(basisController)),
    _basisController(basisController),
    _eigenvalues(new SpinPolarizedData<SCFMode, Eigen::VectorXd>(basisController->getNBasisFunctions())),
    _isCoreOrbital(new SpinPolarizedData<SCFMode, Eigen::VectorXi>(basisController->getNBasisFunctions())),
    _canOrthTh(1.0e-7),
    _linearDependent(false),
    _nZero(0),
    _fBaseName(filePath),
    _id(id) {
  fromHDF5(filePath, id);
  _basisController->addSensitiveObject(this->_self);
}
template<Options::SCF_MODES SCFMode>
OrbitalController<SCFMode>::~OrbitalController() {
}

template<Options::SCF_MODES SCFMode>
unsigned int OrbitalController<SCFMode>::getNOrbitals() const {
  return _basisController->getNBasisFunctions();
}

template<Options::SCF_MODES SCFMode>
void OrbitalController<SCFMode>::calculateTransformationX(std::shared_ptr<OneElectronIntegralController> oneIntController) {
  if (_X.cols() > 0)
    return;
  // Calculate canonical orthogonalization matrix
  // Note symmetrization is already done in the oneElectronIntegralController
  Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> es(oneIntController->getOverlapIntegrals());
  auto U = es.eigenvectors();
  auto s = es.eigenvalues();
  int n = s.rows();
  _nZero = 0;
  Eigen::VectorXd sigma(n);
  Eigen::VectorXd sigmaInvers(n);
  sigma.setZero();
  sigmaInvers.setZero();
  for (int i = n - 1; i >= 0; --i) {
    if (s(i) > _canOrthTh) {
      sigma[i] = s[i];
      sigmaInvers[i] = 1 / s[i];
      ++_nZero;
    }
    else {
      i = 0;
    }
  }
  _X = Eigen::MatrixXd(U * sigmaInvers.array().sqrt().matrix().asDiagonal()).eval();
  _X = _X.rightCols(_nZero).eval();
  _Xinv = Eigen::MatrixXd(U * sigma.array().sqrt().matrix().asDiagonal()).eval();
  _Xinv = _Xinv.rightCols(_nZero).eval();
  _nZero = n - _nZero;
  _linearDependent = _nZero > 0;
  if (_linearDependent) {
    WarningTracker::printWarning((std::string) "Warning: Basis-Set (near) linear dependent. Will try to use canonical "
                                               "orthogonalization. Removed " +
                                     _nZero + " columns from transformation matrix.",
                                 true);
  }
}

template<Options::SCF_MODES SCFMode>
void OrbitalController<SCFMode>::updateOrbitals(const FockMatrix<SCFMode>& fockMatrix,
                                                std::shared_ptr<OneElectronIntegralController> oneIntController) {
  auto shift = std::make_pair<Eigen::VectorXd, SpinPolarizedData<SCFMode, Eigen::VectorXd>>(
      Eigen::VectorXd(Eigen::VectorXd::Zero(2)), SpinPolarizedData<SCFMode, Eigen::VectorXd>(Eigen::VectorXd(0)));
  this->updateOrbitals(shift, fockMatrix, oneIntController);
}

template<Options::SCF_MODES SCFMode>
void OrbitalController<SCFMode>::updateOrbitals(const std::pair<Eigen::VectorXd, SpinPolarizedData<SCFMode, Eigen::VectorXd>> levelshift,
                                                const FockMatrix<SCFMode>& fockMatrix,
                                                std::shared_ptr<OneElectronIntegralController> oneIntController) {
  Timings::takeTime("Tech. -    Fock Matrix Solving");

  auto& occupation = levelshift.second;

  // Calculate transformation matrix
  if (!_fIsInOthoBasis and !_customS)
    calculateTransformationX(oneIntController);

  auto eps(std::move(this->getEigenvalues()));
  auto c(std::move(this->getCoefficients()));

  if (_customS) {
    const auto& S = *_customS;
    for_spin(fockMatrix, eps, c, S) {
      Eigen::GeneralizedSelfAdjointEigenSolver<Eigen::MatrixXd> es(fockMatrix_spin, S_spin);
      c_spin = es.eigenvectors();
      eps_spin = es.eigenvalues();
    };
  }
  else if (_fIsInOthoBasis) {
    for_spin(fockMatrix, eps, c) {
      Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> es(fockMatrix_spin);
      c_spin = es.eigenvectors();
      eps_spin = es.eigenvalues();
    };
  }
  else {
    for_spin(fockMatrix, eps, c, occupation) {
      // transform F into orthonormal basis
      Eigen::MatrixXd tmpFockMatrix(_X.transpose() * fockMatrix_spin * _X);

      // Level-shift
      if (levelshift.first[0] > 0.0 and !_firstIteration) {
        auto C_ortho = _Xinv.transpose() * c_spin;
        // transform into old MO space
        tmpFockMatrix = C_ortho.transpose() * tmpFockMatrix * C_ortho;
        const double diagonalShift = levelshift.first[0];
        for (unsigned int i = 0; i < occupation_spin.size(); i++) {
          if (occupation_spin[i] < 1e-9) {
            tmpFockMatrix(i, i) += diagonalShift;
            for (unsigned int j = 0; j < occupation_spin.size(); j++) {
              if (occupation_spin[j] > 1e-9) {
                tmpFockMatrix(i, j) *= levelshift.first[1];
                tmpFockMatrix(j, i) *= levelshift.first[1];
              }
            }
          }
        }
        // transform back from old MO space
        tmpFockMatrix = C_ortho * tmpFockMatrix * C_ortho.transpose();
      }

      Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> es(tmpFockMatrix);
      c_spin.setZero();
      Eigen::MatrixXd newCoeffs = _X * es.eigenvectors();
      for (unsigned int i = 0; i < newCoeffs.cols(); ++i) {
        if (newCoeffs(0, i) < 0.0)
          newCoeffs.col(i) *= -1.0;
      }
      c_spin.leftCols(newCoeffs.cols()) = newCoeffs;
      eps_spin = Eigen::VectorXd::Constant(_basisController->getNBasisFunctions(), std::numeric_limits<double>::infinity());
      Eigen::VectorXd newEigenvalues = es.eigenvalues();
      eps_spin.segment(0, newEigenvalues.size()) = newEigenvalues;
    };
  }
  _firstIteration = false;
  this->updateOrbitals(c, eps);
  Timings::timeTaken("Tech. -    Fock Matrix Solving");
}

template<Options::SCF_MODES SCFMode>
void OrbitalController<SCFMode>::setDiskMode(bool diskmode, std::string fBaseName, std::string id) {
  if (diskmode) {
    _fBaseName = fBaseName;
    _id = id;
    if (_fBaseName.empty())
      throw SerenityError("Need to set file path when setting OrbitalController to disk mode.");
    if (_id.empty())
      throw SerenityError("Need to set file ID when setting OrbitalController to disk mode.");
  }
  if (diskmode and _keepInMemory) {
    this->toHDF5(_fBaseName, _id);
    _coefficients.reset();
    _eigenvalues.reset();
    _isCoreOrbital.reset();
  }
  else if (!diskmode and !_keepInMemory) {
    fromHDF5(_fBaseName, _id);
  }
  _keepInMemory = !diskmode;
}

/// @returns the coefficients determining the orbitals in connection with the basis.
template<Options::SCF_MODES SCFMode>
CoefficientMatrix<SCFMode> OrbitalController<SCFMode>::getCoefficients() {
  if (!_keepInMemory && (_coefficients == nullptr)) {
    assert(!_fBaseName.empty());
    assert(!_id.empty());
    this->coefficientsfromHDF5(_fBaseName, _id);
    CoefficientMatrix<SCFMode> coefficients(*_coefficients);
    _coefficients.reset();
    return coefficients;
  }
  else {
    return (CoefficientMatrix<SCFMode>)*_coefficients;
  }
}

/// @returns the orbital energies.
template<Options::SCF_MODES SCFMode>
SpinPolarizedData<SCFMode, Eigen::VectorXd> OrbitalController<SCFMode>::getEigenvalues() {
  if (!_keepInMemory && (_eigenvalues == nullptr)) {
    assert(!_fBaseName.empty());
    assert(!_id.empty());
    this->eigenvaluesfromHDF5(_fBaseName, _id);
    SpinPolarizedData<SCFMode, Eigen::VectorXd> eigenvalues(*_eigenvalues);
    _eigenvalues.reset();
    return eigenvalues;
  }
  else {
    return (SpinPolarizedData<SCFMode, Eigen::VectorXd>)*_eigenvalues;
  }
}
template<Options::SCF_MODES SCFMode>
SpinPolarizedData<SCFMode, Eigen::VectorXi> OrbitalController<SCFMode>::getCoreOrbitals() {
  if (!_keepInMemory && (_isCoreOrbital == nullptr)) {
    assert(!_fBaseName.empty());
    assert(!_id.empty());
    this->coreOrbitalsfromHDF5(_fBaseName, _id);
    SpinPolarizedData<SCFMode, Eigen::VectorXi> isCoreOrbital(*_isCoreOrbital);
    _isCoreOrbital.reset();
    return isCoreOrbital;
  }
  else {
    return (SpinPolarizedData<SCFMode, Eigen::VectorXi>)*_isCoreOrbital;
  }
}

template<Options::SCF_MODES SCFMode>
void OrbitalController<SCFMode>::updateOrbitals(const CoefficientMatrix<SCFMode>& updatedCoefficients,
                                                const SpinPolarizedData<SCFMode, Eigen::VectorXd>& updatedEigenvalues,
                                                SpinPolarizedData<SCFMode, Eigen::VectorXi> coreOrbitals) {
  assert(updatedCoefficients.getBasisController() == _basisController);
  bool keeptmp = _keepInMemory;
  if (!_keepInMemory) {
    assert(!_fBaseName.empty());
    assert(!_id.empty());
    _eigenvalues.reset(new SpinPolarizedData<SCFMode, Eigen::VectorXd>(_basisController->getNBasisFunctions()));
    _coefficients.reset(new CoefficientMatrix<SCFMode>(_basisController));
    _isCoreOrbital = std::make_unique<SpinPolarizedData<SCFMode, Eigen::VectorXi>>(
        Eigen::VectorXi::Zero(_basisController->getNBasisFunctions()));
    _keepInMemory = true;
  }
  *_eigenvalues = updatedEigenvalues;
  *_coefficients = updatedCoefficients;
  *_isCoreOrbital = coreOrbitals;
  //  auto& isCoreOrbital = *_isCoreOrbital;
  //  for_spin(coreOrbitals,isCoreOrbital) {
  //    if(coreOrbitals_spin.size() > 0) {
  //      if(coreOrbitals_spin.size() != _basisController->getNBasisFunctions()) {
  //        throw SerenityError("ERROR: Incorrect dimensions in the orbital controller for the core orbital
  //        assignment.");
  //      }
  //      isCoreOrbital_spin = coreOrbitals_spin;
  //    }
  //  };
  this->notifyObjects();

  _keepInMemory = keeptmp;
  if (!_keepInMemory) {
    this->toHDF5(_fBaseName, _id);
    _eigenvalues.reset();
    _isCoreOrbital.reset();
  }
}

template<Options::SCF_MODES SCFMode>
void OrbitalController<SCFMode>::updateOrbitals(const CoefficientMatrix<SCFMode>& updatedCoefficients,
                                                const SpinPolarizedData<SCFMode, Eigen::VectorXd>& updatedEigenvalues) {
  this->updateOrbitals(updatedCoefficients, updatedEigenvalues, this->getCoreOrbitals());
}

template<Options::SCF_MODES SCFMode>
void OrbitalController<SCFMode>::fromHDF5(std::string fBaseName, std::string id) {
  this->coefficientsfromHDF5(fBaseName, id);
  this->eigenvaluesfromHDF5(fBaseName, id);
  this->coreOrbitalsfromHDF5(fBaseName, id);
  _firstIteration = false;
}

template<>
void OrbitalController<Options::SCF_MODES::RESTRICTED>::coefficientsfromHDF5(std::string fBaseName, std::string id) {
  HDF5::Filepath name(fBaseName + ".orbs.res.h5");
  HDF5::H5File file(name.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
  HDF5::dataset_exists(file, "coefficients");
  HDF5::attribute_exists(file, "ID");
  HDF5::check_attribute(file, "ID", id);
  _coefficients.reset(new CoefficientMatrix<Options::SCF_MODES::RESTRICTED>(_basisController));
  HDF5::load(file, "coefficients", *_coefficients);
  file.close();
}
template<>
void OrbitalController<Options::SCF_MODES::UNRESTRICTED>::coefficientsfromHDF5(std::string fBaseName, std::string id) {
  HDF5::Filepath name(fBaseName + ".orbs.unres.h5");
  HDF5::H5File file(name.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
  HDF5::dataset_exists(file, "coefficients_alpha");
  HDF5::dataset_exists(file, "coefficients_beta");
  HDF5::attribute_exists(file, "ID");
  HDF5::check_attribute(file, "ID", id);
  _coefficients.reset(new CoefficientMatrix<Options::SCF_MODES::UNRESTRICTED>(_basisController));
  HDF5::load(file, "coefficients_alpha", _coefficients->alpha);
  HDF5::load(file, "coefficients_beta", _coefficients->beta);
  file.close();
}

template<>
void OrbitalController<Options::SCF_MODES::RESTRICTED>::toHDF5(std::string fBaseName, std::string id) {
  std::string name = fBaseName + ".orbs.res.h5";
  HDF5::H5File file(name.c_str(), H5F_ACC_TRUNC);
  HDF5::save(file, "eigenvalues", *_eigenvalues);
  HDF5::save(file, "coefficients", *_coefficients);
  HDF5::save(file, "coreOrbitals", *_isCoreOrbital);
  HDF5::save_scalar_attribute(file, "ID", id);
  file.close();
}
template<>
void OrbitalController<Options::SCF_MODES::UNRESTRICTED>::toHDF5(std::string fBaseName, std::string id) {
  std::string name = fBaseName + ".orbs.unres.h5";
  HDF5::H5File file(name.c_str(), H5F_ACC_TRUNC);
  HDF5::save(file, "eigenvalues_alpha", _eigenvalues->alpha);
  HDF5::save(file, "eigenvalues_beta", _eigenvalues->beta);
  HDF5::save(file, "coefficients_alpha", _coefficients->alpha);
  HDF5::save(file, "coefficients_beta", _coefficients->beta);
  HDF5::save(file, "coreOrbitals_alpha", _isCoreOrbital->alpha);
  HDF5::save(file, "coreOrbitals_beta", _isCoreOrbital->beta);
  HDF5::save_scalar_attribute(file, "ID", id);
  file.close();
}
template<>
void OrbitalController<Options::SCF_MODES::RESTRICTED>::eigenvaluesfromHDF5(std::string fBaseName, std::string id) {
  HDF5::Filepath name(fBaseName + ".orbs.res.h5");
  HDF5::H5File file(name.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
  HDF5::dataset_exists(file, "eigenvalues");
  HDF5::attribute_exists(file, "ID");
  HDF5::check_attribute(file, "ID", id);
  _eigenvalues.reset(
      new SpinPolarizedData<Options::SCF_MODES::RESTRICTED, Eigen::VectorXd>(_basisController->getNBasisFunctions()));
  HDF5::load(file, "eigenvalues", *_eigenvalues);
  file.close();
}
template<>
void OrbitalController<Options::SCF_MODES::UNRESTRICTED>::eigenvaluesfromHDF5(std::string fBaseName, std::string id) {
  HDF5::Filepath name(fBaseName + ".orbs.unres.h5");
  HDF5::H5File file(name.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
  HDF5::dataset_exists(file, "eigenvalues_alpha");
  HDF5::dataset_exists(file, "eigenvalues_beta");
  HDF5::attribute_exists(file, "ID");
  HDF5::check_attribute(file, "ID", id);
  _eigenvalues.reset(
      new SpinPolarizedData<Options::SCF_MODES::UNRESTRICTED, Eigen::VectorXd>(_basisController->getNBasisFunctions()));
  HDF5::load(file, "eigenvalues_alpha", _eigenvalues->alpha);
  HDF5::load(file, "eigenvalues_beta", _eigenvalues->beta);
  file.close();
}
template<>
void OrbitalController<Options::SCF_MODES::RESTRICTED>::coreOrbitalsfromHDF5(std::string fBaseName, std::string id) {
  _isCoreOrbital = std::make_unique<SpinPolarizedData<Options::SCF_MODES::RESTRICTED, Eigen::VectorXi>>(
      Eigen::VectorXi::Zero(_basisController->getNBasisFunctions()));
  try {
    HDF5::Filepath name(fBaseName + ".orbs.res.h5");
    HDF5::H5File file(name.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
    HDF5::dataset_exists(file, "coreOrbitals");
    HDF5::attribute_exists(file, "ID");
    HDF5::check_attribute(file, "ID", id);
    HDF5::load(file, "coreOrbitals", *_isCoreOrbital);
    file.close();
  }
  catch (...) {
    OutputControl::dOut
        << "Small Warning: Old orbital file format detected! Information about core orbitals will not be loaded!"
        << std::endl;
    OutputControl::dOut << "               An energy cut-off of -5 Eh will be used instead." << std::endl;
    this->setCoreOrbitalsByEnergyCutOff(-5);
  }
}
template<>
void OrbitalController<Options::SCF_MODES::UNRESTRICTED>::coreOrbitalsfromHDF5(std::string fBaseName, std::string id) {
  _isCoreOrbital = std::make_unique<SpinPolarizedData<Options::SCF_MODES::UNRESTRICTED, Eigen::VectorXi>>(
      Eigen::VectorXi::Zero(_basisController->getNBasisFunctions()));
  try {
    HDF5::Filepath name(fBaseName + ".orbs.unres.h5");
    HDF5::H5File file(name.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
    HDF5::dataset_exists(file, "coreOrbitals_alpha");
    HDF5::dataset_exists(file, "coreOrbitals_beta");
    HDF5::attribute_exists(file, "ID");
    HDF5::check_attribute(file, "ID", id);
    HDF5::load(file, "coreOrbitals_alpha", _isCoreOrbital->alpha);
    HDF5::load(file, "coreOrbitals_beta", _isCoreOrbital->beta);
    file.close();
  }
  catch (...) {
    OutputControl::dOut
        << "Small Warning: Old orbital file format detected! Information about core orbitals will not be loaded!"
        << std::endl;
    OutputControl::dOut << "               An energy cut-off of -5 Eh will be used instead." << std::endl;
    this->setCoreOrbitalsByEnergyCutOff(-5);
  }
}

template<Options::SCF_MODES SCFMode>
void OrbitalController<SCFMode>::notify() {
  _X.resize(0, 0);
  _firstIteration = true;
  this->notifyObjects();
}

template<Options::SCF_MODES SCFMode>
SpinPolarizedData<SCFMode, Eigen::VectorXi>
OrbitalController<SCFMode>::getCoreOrbitalsByEigenvalue(unsigned int nCoreElectrons,
                                                        const SpinPolarizedData<SCFMode, Eigen::VectorXd>& eigenvalues) {
  unsigned int nCoreOrbitals = nCoreElectrons / 2;
  SpinPolarizedData<SCFMode, Eigen::VectorXi> isCoreOrbital;
  for_spin(isCoreOrbital, eigenvalues) {
    Eigen::VectorXd eps = eigenvalues_spin;
    isCoreOrbital_spin = Eigen::VectorXi::Zero(eps.size());
    for (unsigned int iCore = 0; iCore < nCoreOrbitals; ++iCore) {
      int minIndex;
      eps.minCoeff(&minIndex);
      eps[minIndex] = std::numeric_limits<double>::infinity();
      isCoreOrbital_spin[minIndex] = 1;
    } // for iCore
  };
  return isCoreOrbital;
}

template<Options::SCF_MODES SCFMode>
void OrbitalController<SCFMode>::setCoreOrbitalsByNumber(unsigned int nCoreOrbitals) {
  if (!_isCoreOrbital)
    _isCoreOrbital = std::make_unique<SpinPolarizedData<SCFMode, Eigen::VectorXi>>(
        Eigen::VectorXi::Zero(_basisController->getNBasisFunctions()));
  *_isCoreOrbital = getCoreOrbitalsByEigenvalue(nCoreOrbitals * 2, this->getEigenvalues());
}

template<Options::SCF_MODES SCFMode>
void OrbitalController<SCFMode>::setCoreOrbitalsByEnergyCutOff(double energyCutOff) {
  const auto& eigenvalues = this->getEigenvalues();
  if (!_isCoreOrbital)
    _isCoreOrbital = std::make_unique<SpinPolarizedData<SCFMode, Eigen::VectorXi>>(
        Eigen::VectorXi::Zero(_basisController->getNBasisFunctions()));
  auto& isCoreOrbital = *_isCoreOrbital;
  for_spin(eigenvalues, isCoreOrbital) {
    isCoreOrbital_spin.setZero();
    for (unsigned int iOrb = 0; iOrb < eigenvalues_spin.size(); ++iOrb) {
      if (eigenvalues_spin(iOrb) < energyCutOff) {
        isCoreOrbital_spin(iOrb) = 1;
      }
    }
  }; // for spin
}

template<Options::SCF_MODES SCFMode>
std::pair<SpinPolarizedData<SCFMode, std::vector<unsigned int>>, SpinPolarizedData<SCFMode, std::vector<unsigned int>>>
OrbitalController<SCFMode>::getValenceOrbitalIndices(SpinPolarizedData<SCFMode, unsigned int> nOcc) {
  SpinPolarizedData<SCFMode, std::vector<unsigned int>> valenceRange;
  SpinPolarizedData<SCFMode, std::vector<unsigned int>> coreRange;
  const auto& coreOrbitals = this->getCoreOrbitals();
  for_spin(nOcc, valenceRange, coreRange, coreOrbitals) {
    for (unsigned int iOcc = 0; iOcc < nOcc_spin; ++iOcc) {
      if (!coreOrbitals_spin[iOcc]) {
        valenceRange_spin.push_back(iOcc);
      }
      else {
        coreRange_spin.push_back(iOcc);
      }
    } // for iOcc
  };  // for spin
  return std::make_pair(valenceRange, coreRange);
}

template class OrbitalController<Options::SCF_MODES::RESTRICTED>;
template class OrbitalController<Options::SCF_MODES::UNRESTRICTED>;

} /* namespace Serenity */
