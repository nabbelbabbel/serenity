/**
 * @file LRSCFController.cpp
 *
 * @date Dec 06, 2018
 * @author Michael Boeckers, Niklas Niemeyer, Johannes Toelle
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
#include "postHF/LRSCF/LRSCFController.h"
/* Include Serenity Internal Headers */
#include "basis/AtomCenteredBasisController.h"
#include "data/ElectronicStructure.h"
#include "data/OrbitalController.h"
#include "geometry/Atom.h"
#include "integrals/OneElectronIntegralController.h"
#include "io/FormattedOutputStream.h" //Filtered output.
#include "io/HDF5.h"
#include "math/linearAlgebra/Orthogonalization.h"
#include "postHF/LRSCF/Sigmavectors/RICC2/ADC2Sigmavector.h"
#include "postHF/LRSCF/Sigmavectors/RICC2/CC2Sigmavector.h"
#include "postHF/LRSCF/Sigmavectors/RICC2/XWFController.h"
#include "postHF/LRSCF/Tools/Besley.h"
#include "postHF/LRSCF/Tools/RIIntegrals.h"
#include "postHF/MBPT/MBPT.h"
#include "potentials/bundles/PotentialBundle.h"
#include "settings/LRSCFOptions.h"
#include "settings/Settings.h"
#include "system/SystemController.h"
#include "tasks/LRSCFTask.h"

/* Include Std and External Headers */
#include <numeric>

namespace Serenity {

template<Options::SCF_MODES SCFMode>
LRSCFController<SCFMode>::LRSCFController(std::shared_ptr<SystemController> system, const LRSCFTaskSettings& settings)
  : _system(system),
    _settings(settings),
    _nOcc(_system->getNOccupiedOrbitals<SCFMode>()),
    _nVirt(_system->getNVirtualOrbitalsTruncated<SCFMode>()),
    _coefficients(_system->getActiveOrbitalController<SCFMode>()->getCoefficients()),
    _particleCoefficients(_system->getActiveOrbitalController<SCFMode>()->getCoefficients()),
    _holeCoefficients(_system->getActiveOrbitalController<SCFMode>()->getCoefficients()),
    _orbitalEnergies(_system->getActiveOrbitalController<SCFMode>()->getEigenvalues()),
    _excitationVectors(nullptr),
    _xwfController(nullptr),
    _riints(nullptr),
    _riErfints(nullptr),
    _screening(nullptr),
    _envTransformation(nullptr),
    _inverseM(nullptr),
    _inverseErfM(nullptr) {
  Settings sysSettings = _system->getSettings();
  if (sysSettings.load != "") {
    _system->template getElectronicStructure<SCFMode>()->toHDF5(sysSettings.path + sysSettings.name, sysSettings.identifier);
  }
}

template<Options::SCF_MODES SCFMode>
std::shared_ptr<std::vector<Eigen::MatrixXd>> LRSCFController<SCFMode>::getExcitationVectors(Options::LRSCF_TYPE type) {
  if (!_excitationVectors || _type != type) {
    this->loadFromH5(type);
  }
  return _excitationVectors;
}

template<Options::SCF_MODES SCFMode>
std::shared_ptr<Eigen::VectorXd> LRSCFController<SCFMode>::getExcitationEnergies(Options::LRSCF_TYPE type) {
  if (!_excitationEnergies || _type != type) {
    this->loadFromH5(type);
  }
  return _excitationEnergies;
}

template<Options::SCF_MODES SCFMode>
void LRSCFController<SCFMode>::loadFromH5(Options::LRSCF_TYPE type) {
  auto settings = _system->getSettings();

  std::string tName;
  if (type == Options::LRSCF_TYPE::ISOLATED) {
    tName += "iso";
  }
  else if (type == Options::LRSCF_TYPE::UNCOUPLED) {
    tName += "fdeu";
  }
  else {
    tName += "fdec";
  }

  printSmallCaption("Loading " + tName + "-eigenpairs for: " + settings.name);

  std::string path = (settings.load == "") ? settings.path : settings.load;
  std::string fName = settings.name + "_lrscf.";

  std::vector<Eigen::MatrixXd> XY(2);
  Eigen::VectorXd eigenvalues;

  auto loadEigenpairs = [&](std::string input) {
    printf("\n   $  %-20s\n\n", input.c_str());

    HDF5::H5File file(input.c_str(), H5F_ACC_RDONLY);
    HDF5::dataset_exists(file, "X");
    HDF5::dataset_exists(file, "Y");
    HDF5::dataset_exists(file, "EIGENVALUES");
    HDF5::load(file, "X", XY[0]);
    HDF5::load(file, "Y", XY[1]);
    HDF5::load(file, "EIGENVALUES", eigenvalues);
    file.close();

    unsigned nDimI = 0;
    for_spin(_nOcc, _nVirt) {
      nDimI += _nOcc_spin * _nVirt_spin;
    };
    if (XY[0].rows() != nDimI || XY[1].rows() != nDimI) {
      throw SerenityError("The dimension of your loaded eigenpairs does not match with this response problem.");
    }
    if (eigenvalues.rows() != XY[0].cols()) {
      throw SerenityError("The number of loaded eigenvectors and eigenvalues does not match.");
    }

    printf("  Found %3i eigenpairs.\n\n\n", (int)eigenvalues.rows());

    _excitationEnergies = std::make_shared<Eigen::VectorXd>(eigenvalues);
    _excitationVectors = std::make_shared<std::vector<Eigen::MatrixXd>>(XY);
    _type = type;
  };

  if (_settings.method == Options::LR_METHOD::TDA || _settings.method == Options::LR_METHOD::TDDFT) {
    fName += "tddft." + tName + (SCFMode == RESTRICTED ? ".res.h5" : ".unres.h5");
  }
  else {
    fName += "cc2." + tName + (SCFMode == RESTRICTED ? ".res.h5" : ".unres.h5");
  }

  try {
    loadEigenpairs(path + fName);
  }
  catch (...) {
    if (settings.load != "") {
      printf("  Could not find any. Instead\n");
      try {
        loadEigenpairs(settings.path + fName);
      }
      catch (...) {
        throw SerenityError("Cannot find eigenpairs from h5: " + (settings.path + fName));
      }
    }
    else {
      throw SerenityError("Cannot find eigenpairs from h5: " + (path + fName));
    }
  }
}

template<Options::SCF_MODES SCFMode>
void LRSCFController<SCFMode>::setSolution(std::shared_ptr<std::vector<Eigen::MatrixXd>> eigenvectors,
                                           std::shared_ptr<Eigen::VectorXd> eigenvalues, Options::LRSCF_TYPE type) {
  _excitationVectors = eigenvectors;
  _excitationEnergies = eigenvalues;
  _type = type;
  // write to h5
  std::string fName = this->getSys()->getSystemPath() + this->getSys()->getSystemName() + "_lrscf.";
  if (_settings.method == Options::LR_METHOD::TDA || _settings.method == Options::LR_METHOD::TDDFT) {
    fName += "tddft.";
  }
  else {
    fName += "cc2.";
  }
  if (type == Options::LRSCF_TYPE::ISOLATED) {
    fName += "iso.";
  }
  else if (type == Options::LRSCF_TYPE::UNCOUPLED) {
    fName += "fdeu.";
  }
  else {
    fName += "fdec.";
  }
  fName += (SCFMode == RESTRICTED) ? "res." : "unres.";
  fName += "h5";
  HDF5::H5File file(fName.c_str(), H5F_ACC_TRUNC);
  HDF5::save_scalar_attribute(file, "ID", _system->getSystemIdentifier());
  HDF5::save(file, "X", (*eigenvectors)[0]);
  if (this->getResponseMethod() == Options::LR_METHOD::TDA) {
    Eigen::MatrixXd Y = Eigen::MatrixXd::Zero((*eigenvectors)[0].rows(), (*eigenvectors)[0].cols());
    HDF5::save(file, "Y", Y);
  }
  else if (this->getResponseMethod() == Options::LR_METHOD::TDDFT) {
    HDF5::save(file, "Y", (*eigenvectors)[1]);
  }
  HDF5::save(file, "EIGENVALUES", (*eigenvalues));
  file.close();
}

template<Options::SCF_MODES SCFMode>
SpinPolarizedData<SCFMode, unsigned> LRSCFController<SCFMode>::getNOccupied() {
  return _nOcc;
}

template<Options::SCF_MODES SCFMode>
SpinPolarizedData<SCFMode, unsigned> LRSCFController<SCFMode>::getNVirtual() {
  return _nVirt;
}

template<Options::SCF_MODES SCFMode>
void LRSCFController<SCFMode>::setNOccupied(SpinPolarizedData<SCFMode, unsigned> nOcc) {
  _nOcc = nOcc;
}

template<Options::SCF_MODES SCFMode>
void LRSCFController<SCFMode>::setNVirtual(SpinPolarizedData<SCFMode, unsigned> nVirt) {
  _nVirt = nVirt;
}

template<Options::SCF_MODES SCFMode>
CoefficientMatrix<SCFMode>& LRSCFController<SCFMode>::getCoefficients() {
  return _coefficients;
}

template<Options::SCF_MODES SCFMode>
CoefficientMatrix<SCFMode>& LRSCFController<SCFMode>::getParticleCoefficients() {
  if (_xwfController) {
    auto& P = _xwfController->_P;
    for_spin(P, _particleCoefficients) {
      _particleCoefficients_spin = P_spin;
    };
  }
  else {
    _particleCoefficients = _coefficients;
  }
  return _particleCoefficients;
}

template<Options::SCF_MODES SCFMode>
CoefficientMatrix<SCFMode>& LRSCFController<SCFMode>::getHoleCoefficients() {
  if (_xwfController) {
    auto& H = _xwfController->_H;
    for_spin(H, _holeCoefficients) {
      _holeCoefficients_spin = H_spin;
    };
  }
  else {
    _holeCoefficients = _coefficients;
  }
  return _holeCoefficients;
}

template<Options::SCF_MODES SCFMode>
void LRSCFController<SCFMode>::setCoefficients(CoefficientMatrix<SCFMode> coeff) {
  _coefficients = coeff;
}

template<Options::SCF_MODES SCFMode>
std::shared_ptr<BasisController> LRSCFController<SCFMode>::getBasisController(Options::BASIS_PURPOSES basisPurpose) {
  return _system->getBasisController(basisPurpose);
}

template<Options::SCF_MODES SCFMode>
std::shared_ptr<SPMatrix<SCFMode>> LRSCFController<SCFMode>::getMOFockMatrix() {
  if (!_system->template getElectronicStructure<SCFMode>()->checkFock()) {
    throw SerenityError("lrscf: no fock matrix present in your system.");
  }
  auto fock = _system->template getElectronicStructure<SCFMode>()->getFockMatrix();
  if (!_settings.rpaScreening) {
    // since this doesn't really cost anything, each time the mo fock matrix is
    // requested, it is built again to make sure the newest coefficients are used
    for_spin(_coefficients, fock, _nOcc, _nVirt) {
      unsigned nb = _nOcc_spin + _nVirt_spin;
      fock_spin = (_coefficients_spin.leftCols(nb).transpose() * fock_spin * _coefficients_spin.leftCols(nb)).eval();
    };
  }
  else {
    for_spin(fock, _orbitalEnergies, _nOcc, _nVirt) {
      Eigen::MatrixXd temp = _orbitalEnergies_spin.segment(0, _nOcc_spin + _nVirt_spin).asDiagonal();
      fock_spin = temp;
    };
  }
  _fock = std::make_shared<SPMatrix<SCFMode>>(fock);
  return _fock;
}

template<Options::SCF_MODES SCFMode>
bool LRSCFController<SCFMode>::isMOFockMatrixDiagonal() {
  SPMatrix<SCFMode> fock = *this->getMOFockMatrix();
  bool isDiagonal = true;
  for_spin(fock) {
    fock_spin.diagonal() -= fock_spin.diagonal();
    isDiagonal = isDiagonal && std::fabs(fock_spin.sum()) < 1e-6;
    if (std::fabs(fock_spin.sum()) > 1e-6)
      OutputControl::nOut << " Absolute largest Fock matrix off-Diagonal element  "
                          << fock_spin.array().abs().matrix().maxCoeff() << std::endl;
  };

  return isDiagonal;
}

template<Options::SCF_MODES SCFMode>
SpinPolarizedData<SCFMode, Eigen::VectorXd> LRSCFController<SCFMode>::getEigenvalues() {
  return _orbitalEnergies;
}

template<Options::SCF_MODES SCFMode>
void LRSCFController<SCFMode>::setEigenvalues(SpinPolarizedData<SCFMode, Eigen::VectorXd> eigenvalues) {
  _orbitalEnergies = eigenvalues;
}

template<Options::SCF_MODES SCFMode>
std::shared_ptr<GridController> LRSCFController<SCFMode>::getGridController() {
  return _system->getGridController();
}

template<Options::SCF_MODES SCFMode>
const Settings& LRSCFController<SCFMode>::getSysSettings() {
  return _system->getSettings();
}

template<Options::SCF_MODES SCFMode>
std::shared_ptr<SystemController> LRSCFController<SCFMode>::getSys() {
  return _system;
}

template<Options::SCF_MODES SCFMode>
const LRSCFTaskSettings& LRSCFController<SCFMode>::getLRSCFSettings() {
  return _settings;
}

template<Options::SCF_MODES SCFMode>
Options::LR_METHOD LRSCFController<SCFMode>::getResponseMethod() {
  return _settings.method;
}

template<Options::SCF_MODES SCFMode>
void LRSCFController<SCFMode>::setEnvSystems(std::vector<std::shared_ptr<SystemController>> envSystems) {
  _envSystems = envSystems;
}

template<Options::SCF_MODES SCFMode>
std::vector<std::shared_ptr<SystemController>> LRSCFController<SCFMode>::getEnvSystems() {
  return _envSystems;
}

template<Options::SCF_MODES SCFMode>
void LRSCFController<SCFMode>::initializeXWFController() {
  if (this->getResponseMethod() == Options::LR_METHOD::ADC2) {
    _xwfController = std::make_shared<ADC2Sigmavector<SCFMode>>(this->shared_from_this());
  }
  else {
    _xwfController = std::make_shared<CC2Sigmavector<SCFMode>>(this->shared_from_this());
  }
}

template<Options::SCF_MODES SCFMode>
std::shared_ptr<XWFController<SCFMode>> LRSCFController<SCFMode>::getXWFController() {
  return _xwfController;
}

template<Options::SCF_MODES SCFMode>
void LRSCFController<SCFMode>::initializeRIIntegrals(LIBINT_OPERATOR op, double mu, bool calcJia) {
  if (op == LIBINT_OPERATOR::coulomb) {
    _riints = std::make_shared<RIIntegrals<SCFMode>>(this->shared_from_this(), op, mu, calcJia);
  }
  else if (op == LIBINT_OPERATOR::erf_coulomb) {
    _riErfints = std::make_shared<RIIntegrals<SCFMode>>(this->shared_from_this(), op, mu, calcJia);
  }
  else {
    throw SerenityError("This operator for RI integrals is not yet supported.");
  }
}

template<Options::SCF_MODES SCFMode>
std::shared_ptr<RIIntegrals<SCFMode>> LRSCFController<SCFMode>::getRIIntegrals(LIBINT_OPERATOR op) {
  if (op == LIBINT_OPERATOR::coulomb) {
    return _riints;
  }
  else if (op == LIBINT_OPERATOR::erf_coulomb) {
    return _riErfints;
  }
  else {
    throw SerenityError("This operator for RI integrals is not yet supported.");
  }
}

template<Options::SCF_MODES SCFMode>
void LRSCFController<SCFMode>::calculateScreening(const Eigen::VectorXd& eia) {
  if (_riints != nullptr) {
    printBigCaption("rpa screening");
    auto& jia = *(_riints->getJiaPtr());
    unsigned nAux = _riints->getNTransformedAuxBasisFunctions();
    double prefactor = (SCFMode == Options::SCF_MODES::RESTRICTED) ? 2.0 : 1.0;
    Eigen::MatrixXd piPQ = Eigen::MatrixXd::Identity(nAux, nAux);
    Eigen::VectorXd chi_temp = prefactor * (-2.0 / eia.array());
    unsigned spincounter = 0;
    for_spin(jia) {
      piPQ -= jia_spin.transpose() * chi_temp.segment(spincounter, jia_spin.rows()).asDiagonal() * jia_spin;
      spincounter += jia_spin.rows();
    };
    if (_envSystems.size() > 0) {
      if (_settings.nafThresh != 0) {
        throw SerenityError(" NAF not supported with environmetnal screening!");
      }
      GWTaskSettings gwSettings;
      gwSettings.environmentScreening = false;
      gwSettings.integrationPoints = 0;
      // sets the geometry of the active subsystem to evaluate integrals in the correct aux basis
      _riints->setGeo(_system->getGeometry());
      auto mbpt = std::make_shared<MBPT<SCFMode>>(_system, gwSettings, _envSystems, _riints, 0, 0);
      auto envResponse = mbpt->environmentRespose();
      Eigen::MatrixXd transformation;
      auto proj = mbpt->calculateTransformation(transformation, envResponse);
      _envTransformation = std::make_shared<Eigen::MatrixXd>(transformation * proj.transpose());
      spincounter = 0;
      for_spin(jia) {
        auto piPQenv = (jia_spin * transformation).transpose() *
                       chi_temp.segment(spincounter, jia_spin.rows()).asDiagonal() * (jia_spin * transformation);
        piPQ += proj * piPQenv * proj.transpose();
        spincounter += jia_spin.rows();
      };
    }
    piPQ = (piPQ.inverse()).eval();
    _screening = std::make_shared<Eigen::MatrixXd>(piPQ);
    printf(" .. done.\n\n");
  }
  else {
    throw SerenityError("No RI integrals for screening initialized!");
  }
}

template<Options::SCF_MODES SCFMode>
std::shared_ptr<Eigen::MatrixXd> LRSCFController<SCFMode>::getScreeningAuxMatrix() {
  return _screening;
}

template<Options::SCF_MODES SCFMode>
std::shared_ptr<Eigen::MatrixXd> LRSCFController<SCFMode>::getEnvTrafo() {
  return _envTransformation;
}

template<Options::SCF_MODES SCFMode>
void LRSCFController<SCFMode>::setInverseMetric(std::shared_ptr<Eigen::MatrixXd> metric) {
  _inverseM = metric;
}

template<Options::SCF_MODES SCFMode>
std::shared_ptr<Eigen::MatrixXd> LRSCFController<SCFMode>::getInverseMetric() {
  return _inverseM;
}

template<Options::SCF_MODES SCFMode>
void LRSCFController<SCFMode>::setInverseErfMetric(std::shared_ptr<Eigen::MatrixXd> metric) {
  _inverseErfM = metric;
}

template<Options::SCF_MODES SCFMode>
std::shared_ptr<Eigen::MatrixXd> LRSCFController<SCFMode>::getInverseErfMetric() {
  return _inverseErfM;
}

template<Options::SCF_MODES SCFMode>
void LRSCFController<SCFMode>::editReference(SpinPolarizedData<SCFMode, std::vector<unsigned>> indexWhiteList) {
  unsigned iSpin = 0;
  for_spin(_coefficients, _orbitalEnergies, indexWhiteList, _nOcc, _nVirt) {
    // Update orbitals
    auto oldCoefficients = _coefficients_spin;
    auto oldOrbitalEnergies = _orbitalEnergies_spin;
    _coefficients_spin.setZero();
    _nOcc_spin = 0;
    _nVirt_spin = 0;
    // Adapt occupation vector and orbital energies
    _orbitalEnergies_spin.resize(indexWhiteList_spin.size());

    for (unsigned iMO = 0; iMO < indexWhiteList_spin.size(); ++iMO) {
      _coefficients_spin.col(iMO) = oldCoefficients.col(indexWhiteList_spin[iMO]);
      _orbitalEnergies_spin(iMO) = oldOrbitalEnergies(indexWhiteList_spin[iMO]);
      if (indexWhiteList_spin[iMO] <= _nOcc_spin) {
        _nOcc_spin += 1;
      }
      else {
        _nVirt_spin += 1;
      }
    } /* Update orbitals */
    std::string system = _system->getSettings().name;
    // Print info
    if (SCFMode == Options::SCF_MODES::RESTRICTED) {
      std::cout << " System: " << system << " \n";
      printf(" NEW Reference orbitals : \n");
    }
    else {
      std::cout << " System: " << system << " \n";
      printf("%s NEW Reference orbitals : \n", (iSpin == 0) ? "Alpha" : "Beta");
    }
    for (unsigned iMO = 0; iMO < indexWhiteList_spin.size(); ++iMO) {
      printf("%4i", indexWhiteList_spin[iMO] + 1);
      if ((iMO + 1) % 10 == 0)
        printf("\n");
    }
    printf("\n");
    iSpin += 1;
  };
}

template class LRSCFController<Options::SCF_MODES::RESTRICTED>;
template class LRSCFController<Options::SCF_MODES::UNRESTRICTED>;
} /* namespace Serenity */
