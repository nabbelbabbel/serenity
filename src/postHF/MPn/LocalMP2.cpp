/**
 * @file LocalMP2.cpp
 *
 * @date Dec 7, 2018
 * @author Moritz Bensberg
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
#include "postHF/MPn/LocalMP2.h"
/* Include Serenity Internal Headers */
#include "data/OrbitalController.h"                                 //Coefficients
#include "integrals/transformer/Ao2MoExchangeIntegralTransformer.h" //Integral transformation
#include "io/FormattedOutput.h"                                     //Captions etc.
#include "io/FormattedOutputStream.h"                               //Filtered output streams.
#include "postHF/LocalCorrelation/LocalCorrelationController.h"     //The local correlation controller.
#include "postHF/LocalCorrelation/OrbitalPairDIISWrapper.h"         //DIIS
#include "settings/Settings.h"                                      //System settings
#include "system/SystemController.h"                                //SystemController
/* Prescreening, PAOs and PAO selection */
#include "analysis/PAOSelection/PNOConstructor.h"       //PNO construction
#include "data/OrbitalPair.h"                           //OrbitalPair definition.
#include "postHF/LocalCorrelation/CouplingOrbitalSet.h" //K-Set definition.
/* Include Std and External Headers */
#include <iomanip> //setw(...) for ostream/std::fixed

namespace Serenity {

void LocalMP2::generateExchangeIntegrals(std::vector<std::shared_ptr<OrbitalPair>> orbitalPairs,
                                         std::vector<std::shared_ptr<OrbitalPair>> veryDistantPairs) {
  // Calculate exchange integrals for each pair.
  const auto scfMode = Options::SCF_MODES::RESTRICTED;
  const auto activeSystem = _localCorrelationController->getActiveSystemController();
  unsigned int nOcc = activeSystem->getNOccupiedOrbitals<scfMode>();
  // Calculate all three center integrals
  const Eigen::MatrixXd occCoefficients =
      activeSystem->getActiveOrbitalController<scfMode>()->getCoefficients().leftCols(nOcc);
  printSmallCaption("Integral Calculation");
  if (settings.useFourCenterIntegrals) {
    std::shared_ptr<PNOConstructor> pnoConstructor =
        _localCorrelationController->producePNOConstructor(settings.ssScaling, settings.osScaling);
    Ao2MoExchangeIntegralTransformer::transformExchangeIntegrals(
        activeSystem->getBasisController(),
        activeSystem->getActiveOrbitalController<RESTRICTED>()->getCoefficients().leftCols(
            activeSystem->getNOccupiedOrbitals<RESTRICTED>()),
        _localCorrelationController->getPAOController(), orbitalPairs, pnoConstructor);
  }
  else {
    std::shared_ptr<PNOConstructor> pnoConstructor =
        _localCorrelationController->producePNOConstructor(settings.ssScaling, settings.osScaling);
    Ao2MoExchangeIntegralTransformer::transformExchangeIntegrals(
        activeSystem->getBasisController(Options::BASIS_PURPOSES::AUX_CORREL),
        _localCorrelationController->getMO3CenterIntegralController(), orbitalPairs, pnoConstructor);
  }
  _localCorrelationController->buildOrbitalPairCouplingMap();
  double scMP2Energy = calculateEnergy(orbitalPairs, veryDistantPairs).sum();
  unsigned int nPNOsTot = 0;
  unsigned int nAuxTot = 0;
  for (const auto& pair : orbitalPairs) {
    nPNOsTot += pair->k_ij.rows();
    nAuxTot += pair->nAuxFunctions;
  }
  OutputControl::nOut << std::fixed;
  OutputControl::nOut << "-----------------------------------------------------" << std::endl;
  OutputControl::nOut << " PNO Selection and Integral Generation" << std::endl;
  OutputControl::nOut << "  Average number of PNOs per pair   " << (double)nPNOsTot / (double)orbitalPairs.size() << std::endl;
  OutputControl::nOut << "  Semi-Canonical MP2 energy         " << scMP2Energy << " Hartree" << std::endl;
  OutputControl::nOut << "  Average number of Aux functions   " << nAuxTot / orbitalPairs.size() << std::endl;
  OutputControl::nOut << "  Total number of Aux functions     "
                      << activeSystem->getBasisController(Options::BASIS_PURPOSES::AUX_CORREL)->getNBasisFunctions()
                      << std::endl;
  OutputControl::nOut << "-----------------------------------------------------" << std::endl;
  OutputControl::nOut << std::scientific;
  OutputControl::nOut << "  Calculating overlap matrices                           ...";
  OutputControl::nOut.flush();
  for (auto& pair : orbitalPairs) {
    pair->setOverlapMatrixController(_localCorrelationController->getDomainOverlapMatrixController());
    for (auto& kSet : pair->coupledPairs)
      kSet->setOverlapMatrixController(_localCorrelationController->getDomainOverlapMatrixController());
  }
  OutputControl::nOut << " done" << std::endl;
}

void LocalMP2::optimizeAmplitudes(std::vector<std::shared_ptr<OrbitalPair>> orbitalPairs,
                                  std::vector<std::shared_ptr<OrbitalPair>> veryDistantPairs) {
  const auto scfMode = Options::SCF_MODES::RESTRICTED;
  // Get the fock matrix.
  const auto& f = _localCorrelationController->getFockMatrix();
  // Get the fock matrix block of the occupied orbitals in MO representation.
  const auto activeSystem = _localCorrelationController->getActiveSystemController();
  unsigned int nOcc = activeSystem->getNOccupiedOrbitals<scfMode>();
  const Eigen::MatrixXd actCoef = activeSystem->getActiveOrbitalController<scfMode>()->getCoefficients().leftCols(nOcc);
  const Eigen::MatrixXd f_MO = actCoef.transpose() * f * actCoef;

  double f_cut = _localCorrelationController->getSettings().fockMatrixPrescreeningThresholdd;

  // Start the iterative optimization of the amplitudes.
  double largestResidual = 10;
  unsigned int cycle = 0;
  printSmallCaption("Local MP2 Amplitude Optimization");
  takeTime("Amplitude Optimization");
  OrbitalPairDIISWrapper diis(activeSystem->getSettings().scf.diisMaxStore);
  double oldEnergy = 0.0;
  std::printf("%6s %12s %12s %12s\n", "Cycle", "abs. max. Res.", "Corr. Energy", "Delta E_corr");
  while (largestResidual > settings.maxResidual) {
    takeTime("Amplitude Optimization Cycle");
    largestResidual = 0.0;
    for (auto& pair : orbitalPairs) {
      unsigned int i = pair->i;
      unsigned int j = pair->j;
      // Get the PAO coefficients of the pair locally.
      if (pair->type == OrbitalPairTypes::VERY_DISTANT)
        continue;
      // Calculate the first three terms of the residual
      pair->residual = pair->k_ij;
      std::shared_ptr<Eigen::MatrixXd> residual = std::make_shared<Eigen::MatrixXd>(pair->residual);
      residual->array() += pair->uncoupledTerm.array() * pair->t_ij.array();
      // Calculate the terms in the sum_k in the residual
      // Loop over all k
      std::vector<std::shared_ptr<Eigen::MatrixXd>> increments;
      increments.push_back(residual);

#ifdef _OPENMP
      unsigned int nThreads = omp_get_max_threads();
      for (unsigned int inc = 1; inc < nThreads; ++inc) {
        increments.push_back(std::make_shared<Eigen::MatrixXd>(Eigen::MatrixXd::Zero(residual->rows(), residual->cols())));
      }
      Eigen::setNbThreads(1);
#endif

#pragma omp parallel for schedule(static)
      for (unsigned int index = 0; index < pair->coupledPairs.size(); ++index) {
#ifdef _OPENMP
        const unsigned int threadId = omp_get_thread_num();
#else
        const unsigned int threadId = 0;
#endif
        const std::shared_ptr<CouplingOrbitalSet>& coupledSet = pair->coupledPairs[index];
        const unsigned int k = coupledSet->getK();
        const std::shared_ptr<OrbitalPair>& kjPair = coupledSet->getKJPairScreened();
        if (std::fabs(f_MO(i, k)) >= f_cut && kjPair && i != k) {
          const Eigen::MatrixXd& s_ij_kj = coupledSet->getS_ij_kj();
          // Get the amplitudes for pair kj
          // The relation for the amplitudes is given by t_kj = t_jk.transpose() --> only pairs k >= j are listed.
          // Thus, the amplitudes may have to be transposed. All other matrices associated to the pair are symmetric.
          // Calculate the S x T x S term
          const Eigen::MatrixXd tmp = s_ij_kj * ((k >= j) ? kjPair->t_ij : kjPair->t_ij.transpose()) * s_ij_kj.transpose();
          *increments[threadId] -= f_MO(i, k) * tmp;
        } // if std::fabs(f_MO(i,k)) > f_cut
        const std::shared_ptr<OrbitalPair>& ikPair = coupledSet->getIKPairScreened();
        if (std::fabs(f_MO(j, k)) >= f_cut && ikPair && j != k) {
          const Eigen::MatrixXd& s_ij_ik = coupledSet->getS_ij_ik();
          // Get the amplitudes for pair ik and transpose if necessary.
          const Eigen::MatrixXd tmp = s_ij_ik * ((i >= k) ? ikPair->t_ij : ikPair->t_ij.transpose()) * s_ij_ik.transpose();
          *increments[threadId] -= f_MO(k, j) * tmp;
        } // if std::fabs(f_MO(j,k)) > f_cut
      }   // for index
#ifdef _OPENMP
      Eigen::setNbThreads(nThreads);
#endif
      for (unsigned int inc = 1; inc < increments.size(); ++inc) {
        *increments[0] += *increments[inc];
      }
      // update amplitudes
      pair->t_ij.array() -= residual->array() / pair->uncoupledTerm.array();
      double maxCoeff = residual->array().abs().maxCoeff();
      if (maxCoeff > largestResidual)
        largestResidual = maxCoeff;
    } // for ijPairIndex
    ++cycle;
    double newEnergy = calculateEnergy(orbitalPairs, veryDistantPairs).sum();
    if (_localCorrelationController->getSettings().diisStartResidual > largestResidual)
      diis.optimize(orbitalPairs, {});
    std::printf("%6d %12f %12f %12f\n", cycle, largestResidual, newEnergy, oldEnergy - newEnergy);
    oldEnergy = newEnergy;
    if (cycle > settings.maxCycles - 1) {
      throw SerenityError((std::string) "Canceling amplitude optimization after " + cycle + " cycles. NOT CONVERGED!!!");
    } // if cycle > _maxCycles-1
    timeTaken(3, "Amplitude Optimization Cycle");
  } // while
  OutputControl::mOut << "Converged!" << std::endl;
  timeTaken(0, "Amplitude Optimization");
}

Eigen::VectorXd LocalMP2::calculateEnergy(std::vector<std::shared_ptr<OrbitalPair>> closePairs,
                                          std::vector<std::shared_ptr<OrbitalPair>> veryDistantPairs) {
  double localMP2PairEnergies = 0;
  for (const auto& pair : closePairs) {
    double ssEnergy =
        (pair->i == pair->j ? 1.0 : 2.0) * ((pair->t_ij - pair->t_ij.transpose()).array() * pair->k_ij.array()).sum();
    double osEnergy = (pair->i == pair->j ? 1.0 : 2.0) * (pair->t_ij.array() * pair->k_ij.array()).sum();
    double pairEnergy = settings.ssScaling * ssEnergy + settings.osScaling * osEnergy;
    localMP2PairEnergies += pairEnergy;
    pair->lMP2PairEnergy = pairEnergy + pair->deltaPNO;
  }
  double dipoleContribution = 0.0;
  for (const auto& pair : veryDistantPairs) {
    if (pair->scMP2PairEnergy != 0.0) {
      dipoleContribution += pair->scMP2PairEnergy;
    }
    else {
      dipoleContribution += pair->dipolePairEnergy;
    }
  }
  double pnoTruncation = 0.0;
  for (const auto& pair : closePairs)
    pnoTruncation += pair->deltaPNO;
  Eigen::VectorXd energies(3);
  energies << localMP2PairEnergies, dipoleContribution, pnoTruncation;
  return energies;
}

Eigen::VectorXd LocalMP2::calculateEnergyCorrection(std::vector<std::shared_ptr<OrbitalPair>> pairs) {
  std::vector<std::shared_ptr<OrbitalPair>> closePairs;
  std::vector<std::shared_ptr<OrbitalPair>> veryDistantPairs;
  for (auto pair : pairs) {
    if (pair->type == OrbitalPairTypes::VERY_DISTANT) {
      veryDistantPairs.push_back(pair);
    }
    else {
      closePairs.push_back(pair);
    }
  } // for pair
  optimizeAmplitudes(closePairs, veryDistantPairs);
  return calculateEnergy(closePairs, veryDistantPairs);
}

Eigen::VectorXd LocalMP2::calculateEnergyCorrection() {
  auto veryDistantPairs = _localCorrelationController->getOrbitalPairs(OrbitalPairTypes::VERY_DISTANT);
  auto orbitalPairs = _localCorrelationController->getOrbitalPairs(OrbitalPairTypes::CLOSE);
  auto distantPairs = _localCorrelationController->getOrbitalPairs(OrbitalPairTypes::DISTANT);
  orbitalPairs.insert(orbitalPairs.end(), distantPairs.begin(), distantPairs.end());
  generateExchangeIntegrals(orbitalPairs, veryDistantPairs);
  optimizeAmplitudes(orbitalPairs, veryDistantPairs);
  return calculateEnergy(orbitalPairs, veryDistantPairs);
}

} /* namespace Serenity */
