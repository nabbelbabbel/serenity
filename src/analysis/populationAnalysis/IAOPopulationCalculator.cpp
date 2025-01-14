/**
 * @file IAOPopulationCalculator.cpp
 *
 * @date Oct 15, 2018
 * @author Moritz Bensberg
 *
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
#include "analysis/populationAnalysis/IAOPopulationCalculator.h"
/* Include Serenity Internal Headers */
#include "basis/AtomCenteredBasisControllerFactory.h" //MINAO basis construction.
#include "basis/Basis.h"                              //Shell wise populations
#include "data/OrbitalController.h"                   //Orbital coefficient access.
#include "geometry/Geometry.h"                        //Geometry defintion /getNAtoms();
#include "integrals/OneElectronIntegralController.h"  //Overlap integrals.
#include "integrals/wrappers/Libint.h"                //Libint.
#include "math/linearAlgebra/MatrixFunctions.h"       //symmetrize.
#include "settings/Settings.h"                        //Settinge definition.
#include "system/SystemController.h"                  //System controller definition.

namespace Serenity {

template<Options::SCF_MODES SCFMode>
SPMatrix<SCFMode> IAOPopulationCalculator<SCFMode>::calculate1SOrbitalPopulations(std::shared_ptr<SystemController> system) {
  std::shared_ptr<AtomCenteredBasisController> minaoBasis = AtomCenteredBasisControllerFactory::produce(
      system->getGeometry(), system->getSettings().basis.basisLibPath, system->getSettings().basis.makeSphericalBasis,
      false, system->getSettings().basis.firstECP, "MINAO");
  system->setBasisController(minaoBasis, Options::BASIS_PURPOSES::IAO_LOCALIZATION);
  return calculate1SOrbitalPopulations(system->getActiveOrbitalController<SCFMode>()->getCoefficients(),
                                       system->getOneElectronIntegralController()->getOverlapIntegrals(),
                                       system->getNOccupiedOrbitals<SCFMode>(), system->getBasisController(),
                                       minaoBasis, system->getGeometry());
}

template<Options::SCF_MODES SCFMode>
SPMatrix<SCFMode> IAOPopulationCalculator<SCFMode>::calculate1SOrbitalPopulations(
    const CoefficientMatrix<SCFMode>& C, const MatrixInBasis<Options::SCF_MODES::RESTRICTED>& S1,
    const SpinPolarizedData<SCFMode, unsigned int> nOccOrbs, std::shared_ptr<BasisController> B1,
    std::shared_ptr<AtomCenteredBasisController> B2, const std::shared_ptr<Geometry> geom) {
  // Coefficients in IAO basis
  auto CIAO = getCIAOCoefficients(C, S1, nOccOrbs, B1, B2).first;
  // Orbital population per atom
  SPMatrix<SCFMode> orbitalPops;
  for_spin(CIAO, orbitalPops) {
    unsigned int nOcc = CIAO_spin.cols();
    orbitalPops_spin.resize(geom->getNAtoms(), nOcc);
    // Loop over occupied orbitals
    for (unsigned int i = 0; i < nOcc; ++i) {
      // Loop over atoms
      for (unsigned int atomIndex = 0; atomIndex < geom->getNAtoms(); ++atomIndex) {
        auto indices = B2->getBasisIndices();
        double q_Ai = CIAO_spin(indices[atomIndex].first, i) * CIAO_spin(indices[atomIndex].first, i);
        orbitalPops_spin(atomIndex, i) = q_Ai;
      } /* for atomIndex */
    }   /* for i */
  };
  return orbitalPops;
}
template<Options::SCF_MODES SCFMode>
SPMatrix<SCFMode> IAOPopulationCalculator<SCFMode>::calculateAtomwiseOrbitalPopulations(std::shared_ptr<SystemController> system) {
  std::shared_ptr<AtomCenteredBasisController> minaoBasis = AtomCenteredBasisControllerFactory::produce(
      system->getGeometry(), system->getSettings().basis.basisLibPath, system->getSettings().basis.makeSphericalBasis,
      false, system->getSettings().basis.firstECP, "MINAO");
  system->setBasisController(minaoBasis, Options::BASIS_PURPOSES::IAO_LOCALIZATION);
  return calculateAtomwiseOrbitalPopulations(system->getActiveOrbitalController<SCFMode>()->getCoefficients(),
                                             system->getOneElectronIntegralController()->getOverlapIntegrals(),
                                             system->getNOccupiedOrbitals<SCFMode>(), system->getBasisController(),
                                             minaoBasis, system->getGeometry());
}
template<Options::SCF_MODES SCFMode>
SPMatrix<SCFMode> IAOPopulationCalculator<SCFMode>::calculateAtomwiseOrbitalPopulations(
    const CoefficientMatrix<SCFMode>& C, const MatrixInBasis<Options::SCF_MODES::RESTRICTED>& S1,
    const SpinPolarizedData<SCFMode, unsigned int> nOccOrbs, std::shared_ptr<BasisController> B1,
    std::shared_ptr<AtomCenteredBasisController> B2, const std::shared_ptr<Geometry> geom) {
  // Coefficients in IAO basis
  auto CIAO = getCIAOCoefficients(C, S1, nOccOrbs, B1, B2).first;
  // Orbital population per atom
  SPMatrix<SCFMode> orbitalPops;
  for_spin(CIAO, orbitalPops) {
    unsigned int nOcc = CIAO_spin.cols();
    orbitalPops_spin.resize(geom->getNAtoms(), nOcc);
    // Loop over occupied orbitals
    for (unsigned int i = 0; i < nOcc; ++i) {
      // Loop over atoms
      for (unsigned int atomIndex = 0; atomIndex < geom->getNAtoms(); ++atomIndex) {
        auto indices = B2->getBasisIndices();
        double q_Ai = 0;
        for (unsigned int mu = indices[atomIndex].first; mu < indices[atomIndex].second; ++mu) {
          q_Ai += CIAO_spin(mu, i) * CIAO_spin(mu, i);
        }
        orbitalPops_spin(atomIndex, i) = q_Ai;
      } /* for atomIndex */
    }   /* for i */
  };
  return orbitalPops;
}
template<Options::SCF_MODES SCFMode>
SPMatrix<SCFMode> IAOPopulationCalculator<SCFMode>::calculateShellwiseOrbitalPopulations(
    const CoefficientMatrix<SCFMode>& C, const MatrixInBasis<Options::SCF_MODES::RESTRICTED>& S1,
    const SpinPolarizedData<SCFMode, unsigned int> nOccOrbs, std::shared_ptr<BasisController> B1,
    std::shared_ptr<AtomCenteredBasisController> B2) {
  // Coefficients in IAO basis
  auto CIAO = getCIAOCoefficients(C, S1, nOccOrbs, B1, B2).first;
  // Orbital population per shell
  SPMatrix<SCFMode> orbitalPops;
  for_spin(CIAO, orbitalPops) {
    unsigned int nOcc = CIAO_spin.cols();
    orbitalPops_spin.resize(B2->getReducedNBasisFunctions(), nOcc);
    // Loop over occupied orbitals
    auto shells = B2->getBasis();
    for (unsigned int i = 0; i < nOcc; ++i) {
      // Loop over shells
      for (unsigned int shellIndex = 0; shellIndex < B2->getReducedNBasisFunctions(); ++shellIndex) {
        double q_Ai = 0;
        unsigned int nCont = shells[shellIndex]->getNContracted();
        for (unsigned int mu = B2->extendedIndex(shellIndex); mu < B2->extendedIndex(shellIndex) + nCont; ++mu) {
          q_Ai += CIAO_spin(mu, i) * CIAO_spin(mu, i);
        }
        orbitalPops_spin(shellIndex, i) = q_Ai;
      } /* for shellIndex */
    }   /* for i */
  };
  return orbitalPops;
}

template<Options::SCF_MODES SCFMode>
SPMatrix<SCFMode>
IAOPopulationCalculator<SCFMode>::calculateShellwiseOrbitalPopulations(std::shared_ptr<SystemController> system) {
  std::shared_ptr<AtomCenteredBasisController> minaoBasis = AtomCenteredBasisControllerFactory::produce(
      system->getGeometry(), system->getSettings().basis.basisLibPath, system->getSettings().basis.makeSphericalBasis,
      false, system->getSettings().basis.firstECP, "MINAO");
  system->setBasisController(minaoBasis, Options::BASIS_PURPOSES::IAO_LOCALIZATION);
  return calculateShellwiseOrbitalPopulations(system->getActiveOrbitalController<SCFMode>()->getCoefficients(),
                                              system->getOneElectronIntegralController()->getOverlapIntegrals(),
                                              system->getNOccupiedOrbitals<SCFMode>(), system->getBasisController(),
                                              minaoBasis);
}

template<Options::SCF_MODES SCFMode>
SpinPolarizedData<SCFMode, Eigen::VectorXd>
IAOPopulationCalculator<SCFMode>::calculateIAOPopulations(std::shared_ptr<SystemController> system) {
  // Calculate orbital wise charges
  auto orbitalWiseCharges = calculateAtomwiseOrbitalPopulations(system);
  // Sum over orbitals.
  SpinPolarizedData<SCFMode, Eigen::VectorXd> totalCharges;
  double spinfactor = (SCFMode == Options::SCF_MODES::RESTRICTED) ? 2.0 : 1.0;
  for_spin(totalCharges, orbitalWiseCharges) {
    totalCharges_spin = spinfactor * orbitalWiseCharges_spin.rowwise().sum();
  };
  return totalCharges;
}

template<Options::SCF_MODES SCFMode>
std::pair<SPMatrix<SCFMode>, SPMatrix<SCFMode>>
IAOPopulationCalculator<SCFMode>::getCIAOCoefficients(std::shared_ptr<SystemController> system) {
  std::shared_ptr<AtomCenteredBasisController> minaoBasis = AtomCenteredBasisControllerFactory::produce(
      system->getGeometry(), system->getSettings().basis.basisLibPath, system->getSettings().basis.makeSphericalBasis,
      system->getSettings().basis.firstECP, false, "MINAO");
  system->setBasisController(minaoBasis, Options::BASIS_PURPOSES::IAO_LOCALIZATION);
  return getCIAOCoefficients(system->getActiveOrbitalController<SCFMode>()->getCoefficients(),
                             system->getOneElectronIntegralController()->getOverlapIntegrals(),
                             system->getNOccupiedOrbitals<SCFMode>(), system->getBasisController(), minaoBasis);
}

template<Options::SCF_MODES SCFMode>
std::pair<SPMatrix<SCFMode>, SPMatrix<SCFMode>>
IAOPopulationCalculator<SCFMode>::getCIAOCoefficients(const CoefficientMatrix<SCFMode>& C,
                                                      const MatrixInBasis<Options::SCF_MODES::RESTRICTED>& S1,
                                                      const SpinPolarizedData<SCFMode, unsigned int> nOccOrbs,
                                                      std::shared_ptr<BasisController> B1,
                                                      std::shared_ptr<BasisController> B2) {
  /*
   * Gather and calculate overlap integrals
   */
  auto& libint = Libint::getInstance();
  Eigen::MatrixXd S2 = symmetrize(libint.compute1eInts(LIBINT_OPERATOR::overlap, B2, B2));
  Eigen::MatrixXd S12 = libint.compute1eInts(LIBINT_OPERATOR::overlap, B2, B1);

  // Coefficients in IAO basis
  SPMatrix<SCFMode> CIAO;
  SPMatrix<SCFMode> othoA;
  //  projection from basis 1 to basis 2 (P12)
  Eigen::MatrixXd P12 = S1.ldlt().solve(S12);
  Eigen::MatrixXd P21 = S2.ldlt().solve(S12.transpose());
  for_spin(C, nOccOrbs, CIAO, othoA) {
    CIAO_spin.resize(B2->getNBasisFunctions(), nOccOrbs_spin);
    /*
     * EQ 1
     */
    Eigen::MatrixXd Ct;
    Eigen::MatrixXd tmp = P12 * P21 * C_spin.leftCols(nOccOrbs_spin);
    // Symmetric orthogonalization
    {
      const Eigen::MatrixXd sym = symmetrize((tmp.transpose() * S1 * tmp).eval());
      Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> es(sym);
      Ct = tmp * es.operatorInverseSqrt();
    }

    /*
     * EQ 2
     */
    // slow but exact:
    Eigen::MatrixXd CCS = C_spin.leftCols(nOccOrbs_spin) * C_spin.leftCols(nOccOrbs_spin).transpose() * S1;
    Eigen::MatrixXd CtCtS = Ct * Ct.transpose() * S1;
    tmp = CCS * CtCtS * P12 + (Eigen::MatrixXd::Identity(CCS.cols(), CCS.rows()) - CCS) *
                                  (Eigen::MatrixXd::Identity(CtCtS.cols(), CtCtS.rows()) - CtCtS) * P12;
    // alternative (faster, but approximate)
    // tmp = P12 + (C_spin.leftCols(nOccOrbs_spin)*C_spin.leftCols(nOccOrbs_spin).transpose()- Ct*Ct.transpose())*S12;
    // Symmetric orthogonalization
    { // these brackets make sure the solver is delete when its used and not needed
      const Eigen::MatrixXd sym = symmetrize((tmp.transpose() * S1 * tmp).eval());
      Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> es(sym);
      othoA_spin = tmp * es.operatorInverseSqrt();
    }

    // transform occupied MOs in C into IAOs
    CIAO_spin = (othoA_spin.transpose() * S1 * C_spin.leftCols(nOccOrbs_spin)).eval();
  };
  return std::make_pair(CIAO, othoA);
}

template class IAOPopulationCalculator<Options::SCF_MODES::RESTRICTED>;
template class IAOPopulationCalculator<Options::SCF_MODES::UNRESTRICTED>;
} /* namespace Serenity */
