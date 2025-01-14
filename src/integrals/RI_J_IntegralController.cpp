/**
 * @file RI_J_IntegralController.cpp
 * @author: Kevin Klahr
 *
 * @date 8. March 2016
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
#include "integrals/RI_J_IntegralController.h"
/* Include Serenity Internal Headers */
#include "basis/Basis.h"
#include "basis/BasisController.h"
#include "data/matrices/DensityMatrix.h"
#include "data/matrices/FockMatrix.h"
#include "integrals/wrappers/Libint.h"
#include "math/linearAlgebra/MatrixFunctions.h" //Pseudo inverse and pseudo inverse sqrt.
#include "misc/Timing.h"
/* Include Std and External Headers */
#include <cassert>

namespace Serenity {

RI_J_IntegralController::RI_J_IntegralController(std::shared_ptr<BasisController> basisControllerA,
                                                 std::shared_ptr<BasisController> auxBasisController,
                                                 std::shared_ptr<BasisController> basisControllerB, LIBINT_OPERATOR op,
                                                 double mu)
  : _basisControllerA(basisControllerA),
    _basisControllerB(basisControllerB),
    _auxBasisController(auxBasisController),
    _op(op),
    _mu(mu),
    _nBasisFunctions(basisControllerA->getNBasisFunctions()),
    _nAuxFunctions(auxBasisController->getNBasisFunctions()),
    _nAuxFunctionsRed(auxBasisController->getReducedNBasisFunctions()),
    _inverseM(0, 0),
    _memManager(MemoryManager::getInstance()) {
  assert(_basisControllerA);
  assert(_auxBasisController);
  initialize();
  _basisControllerA->addSensitiveObject(this->_self);
  _auxBasisController->addSensitiveObject(this->_self);
  if (_basisControllerB)
    _basisControllerB->addSensitiveObject(this->_self);
}

const Eigen::MatrixXd& RI_J_IntegralController::getMetric() {
  calculate2CenterIntegrals();
  return *_M;
}

const Eigen::MatrixXd& RI_J_IntegralController::getInverseM() {
  if (_inverseM.cols() == 0) {
    calculate2CenterIntegrals();
    takeTime("Inversion");
    _inverseM = pseudoInvers_Sym(*_M);
    timeTaken(3, "Inversion");
  }
  return _inverseM;
}

const Eigen::MatrixXd& RI_J_IntegralController::getInverseMSqrt() {
  if (_inverseMSqrt.cols() == 0) {
    calculate2CenterIntegrals();
    takeTime("Inversion and square root");
    _inverseMSqrt = pseudoInversSqrt_Sym(*_M);
    timeTaken(3, "Inversion and square root");
  }
  return _inverseMSqrt;
}

const Eigen::LLT<Eigen::MatrixXd>& RI_J_IntegralController::getLLTMetric() {
  if (!_lltM) {
    _lltM = std::make_shared<Eigen::LLT<Eigen::MatrixXd>>(this->getMetric().llt());
    if (_lltM->info() != Eigen::Success)
      throw SerenityError("Cholesky decomposition failed! Not positive definite!");
  }
  return *_lltM;
}

const std::shared_ptr<BasisController> RI_J_IntegralController::getBasisController() {
  return _basisControllerA;
}

const std::shared_ptr<BasisController> RI_J_IntegralController::getBasisControllerB() {
  return _basisControllerB;
}

const std::shared_ptr<BasisController> RI_J_IntegralController::getAuxBasisController() {
  return _auxBasisController;
}

void RI_J_IntegralController::calculate2CenterIntegrals() {
  assert(!_basisControllerB && "Two center integrals are only available in single basis mode!");
  if (!_M) {
    takeTime("Calc 2-center ints");
    /*
     * Calculate Coulomb metric of aux _basis (P|1/r|Q) (-> eq. [1].(3) )
     */
    _M = std::make_shared<Eigen::MatrixXd>();
    *_M = _libint->compute1eInts(_op, _auxBasisController, _auxBasisController, std::vector<std::shared_ptr<Atom>>(0), _mu);
    timeTaken(3, "Calc 2-center ints");
  }
}

void RI_J_IntegralController::initialize() {
  _M = nullptr;
  _lltM = nullptr;
  _inverseM.resize(0, 0);
  _inverseMSqrt.resize(0, 0);
}

void RI_J_IntegralController::cache3CInts() {
  const bool twoBasisMode = _basisControllerB != nullptr;
  assert(!_cache);
  // check how many ij sets can be stored
  const unsigned int nBFs_A = _basisControllerA->getNBasisFunctions();
  auto memManager = MemoryManager::getInstance();

  long long memPerBlock = (!twoBasisMode) ? nBFs_A * (nBFs_A + 1) / 2 * sizeof(double)
                                          : nBFs_A * _basisControllerB->getNBasisFunctions() * sizeof(double);
  long long freeMem = memManager->getAvailableSystemMemory();
  unsigned long long nBlocks = 0.5 * freeMem / memPerBlock; // keep 50 % of memory here for other things that will be
                                                            // cached
  // return if there was nothing to store
  if (freeMem < 0)
    nBlocks = 0;
  if (nBlocks == 0)
    return;
  if (nBlocks > _auxBasisController->getNBasisFunctions()) {
    nBlocks = _auxBasisController->getNBasisFunctions();
  }
  // store everything that there was space for
  const unsigned int blockSize =
      (!twoBasisMode) ? nBFs_A * (nBFs_A + 1) / 2 : nBFs_A * _basisControllerB->getNBasisFunctions();
  _cache.reset(new Eigen::MatrixXd(Eigen::MatrixXd::Zero(nBlocks, blockSize)));
  auto cache = _cache->data();
  const auto basisControllerB = _basisControllerB;
  auto const distribute = [&cache, &nBlocks, &twoBasisMode, &basisControllerB](const unsigned i, const unsigned j,
                                                                               const unsigned K, const double integral,
                                                                               const unsigned threadId) {
    (void)threadId; // no warning, please
    const unsigned long long ijIndex =
        (!twoBasisMode) ? (unsigned long long)(nBlocks * ((i * (i + 1) / 2) + j))
                        : (unsigned long long)(nBlocks * (i * (basisControllerB->getNBasisFunctions()) + j));
    cache[(unsigned long long)K + ijIndex] = integral;
  };
  if (!twoBasisMode) {
    TwoElecThreeCenterIntLooper looper(_op, 0, _basisControllerA, _auxBasisController,
                                       _basisControllerA->getPrescreeningThreshold(),
                                       std::pair<unsigned int, unsigned int>(0, _cache->rows()), _mu);
    looper.loopNoDerivative(distribute);
  }
  else {
    double prescreeningThresholdA = _basisControllerA->getPrescreeningThreshold();
    double prescreeningThresholdB = _basisControllerB->getPrescreeningThreshold();
    double prescreeningThreshold = std::min(prescreeningThresholdA, prescreeningThresholdB);
    ABTwoElecThreeCenterIntLooper looper(_op, 0, _basisControllerA, _basisControllerB, _auxBasisController,
                                         prescreeningThreshold, std::pair<unsigned int, unsigned int>(0, _cache->rows()), _mu);
    looper.loopNoDerivative(distribute);
  }
}

} /* namespace Serenity */
