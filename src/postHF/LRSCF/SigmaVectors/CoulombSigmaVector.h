/**
 * @file CoulombSigmaVector.h
 *
 * @date Dec 07, 2018
 * @author Johannes Toelle
 * @copyright \n
 *  This file is part of the program Serenity.\n\n
 *  Serenity is free software: you can redistribute it and/or modify
 *  it under the terms of the LGNU Lesser General Public License as
 *  published by the Free Software Foundation, either version 3 of
 *  the License, or (at your option) any later version.\n\n
 *  Serenity is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.\n\n
 *  You should have received a copy of the LGNU Lesser General
 *  Public License along with Serenity.
 *  If not, see <http://www.gnu.org/licenses/>.\n
 */

#ifndef LRSCF_COULOMBSIGMAVECTOR
#define LRSCF_COULOMBSIGMAVECTOR

/* Include Serenity Internal Headers */
#include "postHF/LRSCF/SigmaVectors/SigmaVector.h"
#include "settings/Options.h"

/* Include Std and External Headers */
#include <Eigen/Dense>
#include <memory>

namespace Serenity {
/**
 * @class CoulombSigmaVector CoulombSigmaVector.h
 * @brief Performs the calculation of the coulomb sigma vectors:\n
 *          \f$ \tilde{F}_{ij} = \sum_{k l} \tilde{P}_{kl} (ij|kl) \f$
 */
template<Options::SCF_MODES SCFMode> class CoulombSigmaVector : public SigmaVector<SCFMode> {
public:
  /**
   * @brief Constructor
   * @param lrscf A controller for the lrscf calculation
   * @param b The sets of guess vectors. Generally, the response problem is non-Hermitian and one can have two sets of guess vectors\n
   *          for right (X+Y) and left eigenvectors (X-Y). Per convention, these are stored in b[0] and b[1], respectively.\n
   *          For TDA-like problems, the guesses for X are stored in b[0].\n
   *          Note that sigma vectors can, albeit not used in the present implementation, also be calculated for more than two sets of test vectors.
   * @param densityScreeningThreshold A prescreening threshold. Often, the matrix of guess vectors is rather sparse and has contributions\n
   *         from a few subsystems only, i.e. the density matrices of pure environment systems will be close to zero.\n
   *         If the maximum density matrix element of the density matrix of a specific subsystem is lower than this threshold,\n
   *         the calculation of the contribution of that block to the sigma vectors is skipped.
   */
  CoulombSigmaVector(
    std::vector<std::shared_ptr<LRSCFController<SCFMode> > > lrscf,
    std::vector<Eigen::MatrixXd> b,
    const double densityScreeningThreshold);
  virtual ~CoulombSigmaVector() = default;

private:
  /** @brief Calculates the Coulomb pseudo-Fock Matrix contribution of the response matrix with respect to a set of guess vectors.
  *          The following calculation is performed: \f$ \tilde{F}_{ij} = \sum_{k l} \tilde{P}_{kl} (ij|kl) \f$
  *   @param I/J the particular number of the subsystem
  *   @param P_J pseudo Density Matrix, see SigmaVector.h for definition
  */
  std::unique_ptr<std::vector<std::vector<MatrixInBasis<SCFMode> > > > calcF(
    unsigned int I,
    unsigned int J,
    std::unique_ptr<std::vector<std::vector<MatrixInBasis<SCFMode> > > > P_J) final;

};//class CoulombSigmaVector
}//namespace Serenity

#endif /* LRSCF_COULOMBSIGMAVECTOR */