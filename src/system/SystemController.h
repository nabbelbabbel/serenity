/**
 * @file   SystemController.h
 * @author Thomas Dresselhaus, Jan Unsleber
 *
 * @date   20. Juli 2015, 16:51
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
#ifndef SYSTEMCONTROLLER_H
#define	SYSTEMCONTROLLER_H
/* Include Serenity Internal Headers */
#include "grid/AtomCenteredGridController.h"
#include "geometry/Geometry.h"
#include "integrals/OneIntControllerFactory.h"
#include "settings/Options.h"
#include "tasks/ScfTask.h"
#include "settings/Settings.h"
#include "data/SpinPolarizedData.h"
#include "system/System.h"
#include "integrals/RI_J_IntegralControllerFactory.h"
/* Include Std and External Headers */
#include <cassert>
#include <memory>



namespace Serenity {
/* Forward Declarations */
class Atom;
template<Options::SCF_MODES> class PotentialBundle;
struct  Settings;
template<Options::SCF_MODES> class ElectronicStructure;
/**
 * @class SystemController SystemController.h
 * @brief A quite complex class managing all data associated with a System
 * 
 * where the System is basically defined by a Geometry and charge and spin.
 * This controller provides (or takes in) objects which will be associated with the System, but it
 * does not fully control those. E.g. if the geometry changes it is not this controller that
 * takes care of adapting the basis, integrals and so on. Check the Geometry and Basis class and
 * suitable controllers to find these functionalities.
 *
 * Note: do not construct a shared_ptr/unique_ptr of this class, instead use the getSharedPtr() method.
 */
class SystemController : public std::enable_shared_from_this<SystemController>{
public:
  /**
   * @brief Constructor.
   * @param settings
   */
  SystemController(Settings settings);
  /**
   * @brief Constructor.
   * @param geometry
   * @param settings
   */
  SystemController(std::shared_ptr<Geometry> geometry,
      Settings settings);
  /**
   * @brief Default destructor.
   */

  virtual ~SystemController()= default;

  /**
   * @brief Adds up two Systems. The new system will have a
   *        simply added geometry, charge and spin. Settings
   *        will be taken from the system on the left hand side
   *        of the operator.
   *        Furthermore, the orbitals and orbitalenergies are combined,
   *        ordered as follows: lhs_occ,rhs_occ,lhs_virt,rhs_virt.\n
   *
   *        Example:\n
   *        @code
   *        std::shared_ptr<SystemController> sys1;
   *        std::shared_ptr<SystemController> sys2;
   *        std::shared_ptr<SystemController> sys3= *sys1 + *sys2;
   *        @endcode
   * @param rhs The second system.
   * @return A shared pointer on the new system
   */
  std::shared_ptr<SystemController> operator+(
      SystemController& rhs);

  /**
   * A SystemController holds a reference to the only shared_ptr
   * that should ever be used.
   * Warning: do not create additional shared/unique_ptr
   * @return Returns a shared pointer to the current instance of this class
   */
  inline std::shared_ptr<SystemController> getSharedPtr() {
    return shared_from_this();
  }
  /**
   * @param The new SCF mode [RESTRICTED,UNRESTRICTED].
   */
  inline void setSCFMode(Options::SCF_MODES mode) {
    _system->_settings.scfMode = mode;
  }
  /**
   * @returns the name of the controlled molecular system. It should be unique.
   */
  inline std::string getSystemName() {
    return _system->_settings.name;
  }
  /**
   * @returns Returns the path to saved files.
   */
  inline std::string getSystemPath() {
    return _system->_settings.path;
  }
  /**
   * @returns Returns the path to saved files.
   */
  inline std::string getHDF5BaseName() {
    return _system->_settings.path+_system->_settings.name;
  }
  /**
   * @returns the underlying configuration. All data received from this SystemController is
   *          constructed by using this configuration.
   */
   const Settings& getSettings() const;
  /**
   * @brief Based on the charges of the nuclei the number of electrons is adjusted accordingly.
   *
   * @param charge the charge of the whole system. Cannot be greater than the sum of charges of
   *               the nuclei
   */
  void setCharge(int charge);
  /**
   * @brief Sets the systems spin.
   *
   * @param spin the spin of the whole system.
   */
  void setSpin(int spin);
  /**
   * TODO this should be determined automatically
   * @brief sets the type of the last SCF Calculation
   * @param mode SCF calculation type
   */

  /**
   * @brief Reduces memory storage, clearing temporary data, dumping results to disk.
   *
   * This function switched the existing electronic structure to be dumped to disk, and
   *   read/written on each request that is made, also all cached integrals for this system
   *   will b flushed.
   *   This reduces the memory usage significantly for big systems, or a case with many
   *   used systems, however it means reevaluation of many properties/integrals, be sure
   *   that this is what you want when activating this mode.
   *
   * @param diskmode The new disk mode, true equals storage on disk,
   *                  false results in mostly memory storage.
   */
  void setDiskMode(bool diskmode);

  /*
   * Getters
   */
  /// @returns the charge of the molecular system
  inline int getCharge() const {
    return _system->_settings.charge;
  }
  /// @returns the SCF type of the last SCF Calculation, i.e. restricted or unrestricted
  inline enum Options::SCF_MODES getLastSCFMode() const {
    return _system->_lastSCFMode;
  };
  /**
   * @returns The expectation value of the S_z-Operator, i.e. the excess of alpha electrons over
   *          beta electrons. May also be negative, i.e. there may also be more beta electrons than
   *          alpha electrons.
   */
  inline int getSpin() const {
    return _system->_settings.spin;
  }
  /**
   * @brief returns the number of alpha and beta electrons
   * @returns the number of alpha and beta electrons
   */
  inline SpinPolarizedData<Options::SCF_MODES::UNRESTRICTED, unsigned int>
      getNAlphaAndBetaElectrons() const {
    assert(_system->_settings.spin%2 == (int)_system->_nElectrons%2);
    assert(_system->_nElectrons + _system->_settings.spin > 0 &&
            _system->_nElectrons - _system->_settings.spin > 0);
    return makeUnrestrictedFromPieces<unsigned int>(
        (unsigned int)(_system->_nElectrons+_system->_settings.spin)/2,
        (unsigned int)(_system->_nElectrons-_system->_settings.spin)/2);
  }

  /// @returns true iff the system is open shell (i.e. \< S_z \> != 0).
  inline bool isOpenShell() const {
    return (_system->_settings.spin!=0);
  }
  /**
   *  @returns the number of electrons. Note that this number may not be the same as the sum of
   *           the nuclear charges (modified by the total charge of the system) if effective core
   *           potentials are used (the ECPs reduce the effective nuclear charges and thus also
   *           the number of electrons).
   *
   *  See also getNCoreElectrons() in Atom.h
   */
  template <Options::SCF_MODES T>
  SpinPolarizedData<T, unsigned int> getNElectrons() const;
  /// @returns the number of occupied molecular orbitals
  template <Options::SCF_MODES T>
  SpinPolarizedData<T, unsigned int> getNOccupiedOrbitals() const;
  /// @returns the number of virtual molecular orbitals
  template <Options::SCF_MODES T>
  SpinPolarizedData<T, unsigned int> getNVirtualOrbitals();

  /**
   * @param   basisPurpose what you want to use the basis for. May have different specifications,
   *                       e.g. it may be much larger if you want an auxiliary basis to evaluate ´
   *                       Coulombic interactions or something.
   * @returns the associated basis (for the specified basisPurpose)
   */
  std::shared_ptr<BasisController> getBasisController(
      Options::BASIS_PURPOSES basisPurpose = Options::BASIS_PURPOSES::DEFAULT) const;
  /**
   * Currently we only use atom-centered basis sets, but that may change in the future, e.g. if you
   * think about embedded calculations where you have basis functions on atoms this system does not
   * know anything about. Because of that we have this additional function call and interface.
   * 
   * @param   basisPurpose what you want to use the basis for. May have different specifications,
   *                       e.g. it may be much larger if you want an auxiliary basis to evaluate ´
   *                       Coulombic interactions or something.
   * @returns the associated basis (for the specified basisPurpose) with the atom-related
   *          information.
   */
  inline std::shared_ptr<AtomCenteredBasisController> getAtomCenteredBasisController(
      Options::BASIS_PURPOSES basisPurpose = Options::BASIS_PURPOSES::DEFAULT) const {
    if (!_system->_basisControllers[basisPurpose]){
      produceBasisController(basisPurpose);
    }
		return _system->_basisControllers[basisPurpose];
	}
  /**
   * @param   basisPurpose what you want to use the basis for. May have different specifications,
   *                       e.g. it may be much larger if you want an auxiliary basis to evaluate ´
   *                       Coulombic interactions or something.
   * @returns the used controller for one-electron integrals (for the basis with the specified
   *          basisPurpose)
   */
  inline std::shared_ptr<OneElectronIntegralController> getOneElectronIntegralController(
      Options::BASIS_PURPOSES basisPurpose = Options::BASIS_PURPOSES::DEFAULT) const {
    auto& factory = OneIntControllerFactory::getInstance();
		return factory.produce(this->getBasisController(basisPurpose),
		                       this->getGeometry());
	}
  /// @returns the underlying geometry
	inline std::shared_ptr<Geometry> getGeometry() const {
		return _system->_geometry;
	}
  /**
   * @param gridPurpose In some cases it is useful to have a smaller integration grid than the final
   *                    one. E.g. during the SCF-cycles in DFT calculations (or during geometry
   *                    optimizations) a less accurate integration grid is often sufficient, because
   *                    the total energy is much more sensitive to the integration grid than the
   *                    final orbitals. Thus one can save a lot of computational time when
   *                    intermediately working with a smaller grid. However, a final solution should
   *                    always be created with the 'normal' (i.e. DEFAULT) integration grid.
   * @returns the attached gridController with the specifications for gridPurpose
   */
  inline std::shared_ptr<GridController> getGridController(
        Options::GRID_PURPOSES gridPurpose = Options::GRID_PURPOSES::DEFAULT) const {
    if (!_system->_gridControllers[gridPurpose]) produceGridController(gridPurpose);
    return _system->_gridControllers[gridPurpose];
  }
  /**
   * @param gridController The new grid controller.
   * @param gridPurpose In some cases it is useful to have a smaller integration grid than the final
   *                    one. E.g. during the SCF-cycles in DFT calculations (or during geometry
   *                    optimizations) a less accurate integration grid is often sufficient, because
   *                    the total energy is much more sensitive to the integration grid than the
   *                    final orbitals. Thus one can save a lot of computational time when
   *                    intermediately working with a smaller grid. However, a final solution should
   *                    always be created with the 'normal' (i.e. DEFAULT) integration grid.
   */
  void setGridController(std::shared_ptr<GridController> gridController,
                                Options::GRID_PURPOSES gridPurpose = Options::GRID_PURPOSES::DEFAULT) const {
    _system->_gridControllers[gridPurpose] = gridController;
  }
  /**
   * @brief see the method getGridController(); this method returns an atom-centered version
   */
  inline std::shared_ptr<AtomCenteredGridController> getAtomCenteredGridController(
        Options::GRID_PURPOSES gridPurpose = Options::GRID_PURPOSES::DEFAULT) const {
    if (!_system->_gridControllers[gridPurpose]) produceGridController(gridPurpose);
    // TODO hack, an ugly dynamic pointer cast. Should be cleaned away.
    std::shared_ptr<AtomCenteredGridController> result =
        std::dynamic_pointer_cast<AtomCenteredGridController>(_system->_gridControllers[gridPurpose]);
    assert(result);
    return result;
  }
  /// @returns the currently active OrbitalController
  template <Options::SCF_MODES T>
  std::shared_ptr<OrbitalController<T> > getActiveOrbitalController();;
  /**
   *  @brief Runs a SCF if no active ElectronicStructure is available.
   *  @returns the currently active ElectronicStructure
   */
  template <Options::SCF_MODES T>
  std::shared_ptr<ElectronicStructure<T> > getElectronicStructure() ;
  /**
   *  @brief Checks if an ElectronicStructure is available.
   *  @returns Returns true if an ElectronicStructure is present.
   */
  template <Options::SCF_MODES T> bool hasElectronicStructure();
  /*
   * Forwarded getters
   */
	/// @returns the geometry's atoms
	inline const std::vector<std::shared_ptr<Atom> >& getAtoms() const {
		return _system->_geometry->getAtoms();
	}
	/// @returns the geometry's number of atoms
	inline unsigned int getNAtoms() const {
		return _system->_geometry->getAtoms().size();
	}
	/**
	 * @brief This function generates a set of potentials for basic HF/DFT SCF runs.
	 *
	 * If there is no electronic structure present when this function is called then
	 * there will be an initial guess generated and stored as electronic structure.
	 * This is due to the fact that all of the potentials need reference objects.
	 *
	 * @return Returns a set of potentials.
	 */
  template<Options::SCF_MODES SCFMode, Options::ELECTRONIC_STRUCTURE_THEORIES Theory>
	std::shared_ptr<PotentialBundle<SCFMode> >getPotentials(Options::GRID_PURPOSES grid = Options::GRID_PURPOSES::DEFAULT);
  /*
   * Setters
   */

  /// @param electronicStructure will be used in this System from now on
  template<Options::SCF_MODES T>void setElectronicStructure(std::shared_ptr<ElectronicStructure<T> >);

  /**
   * @brief Set a new BasisController
   * It is not possible to override an existing basis controller
   * assign it before it is ever needed, or don't assign it!
   * @param basisController A new basis Controller
   * @param basisPurpose    The purpose of the new BasisController
   */
  void setBasisController(std::shared_ptr<AtomCenteredBasisController> basisController,
        Options::BASIS_PURPOSES basisPurpose = Options::BASIS_PURPOSES::DEFAULT);

  /**
   * @brief Loads an ElectronicStructure from file. System objects
   *        like Geometries, spin and charge are handled in the
   *        Constructor of this class, since they should not change
   *        in the lifetime of this class.
   * @param system A reference to the system.
   */
  void fromHDF5(std::string loadPath);

  /**
   * @brief Prints system information to screen.
   */
  void print();

private:
  void produceBasisController(Options::BASIS_PURPOSES basisPurpose) const;
  void produceGridController(Options::GRID_PURPOSES gridPurpose) const;

  template<Options::SCF_MODES T>void produceScfTask();

  std::unique_ptr<System> _system;
  
  std::unique_ptr<ScfTask<Options::SCF_MODES::RESTRICTED> >
        _restrictedScfTask;
  std::unique_ptr<ScfTask<Options::SCF_MODES::UNRESTRICTED> >
        _unrestrictedScfTask;
};

} /* namespace Serenity */
#endif	/* SYSTEMCONTROLLER_H */