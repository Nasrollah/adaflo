// --------------------------------------------------------------------------
//
// Copyright (C) 2009 - 2016 by the adaflo authors
//
// This file is part of the adaflo library.
//
// The adaflo library is free software; you can use it, redistribute it,
// and/or modify it under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation; either version 2.1 of the
// License, or (at your option) any later version.  The full text of the
// license can be found in the file LICENSE at the top level of the adaflo
// distribution.
//
// --------------------------------------------------------------------------

#ifndef __adaflo_navier_stokes_h
#define __adaflo_navier_stokes_h

#include <deal.II/base/timer.h>
#include <deal.II/base/conditional_ostream.h>
#include <deal.II/base/thread_local_storage.h>

#include <deal.II/lac/trilinos_sparse_matrix.h>
#include <deal.II/lac/constraint_matrix.h>
#include <deal.II/lac/parallel_block_vector.h>

#include <deal.II/dofs/dof_handler.h>
#include <deal.II/fe/fe_system.h>
#include <deal.II/distributed/solution_transfer.h>
#include <deal.II/matrix_free/matrix_free.h>

#include <adaflo/navier_stokes_matrix.h>
#include <adaflo/navier_stokes_preconditioner.h>
#include <adaflo/time_stepping.h>
#include <adaflo/parameters.h>
#include <adaflo/flow_base_algorithm.h>

using namespace dealii;




template <int dim>
class NavierStokes : public FlowBaseAlgorithm<dim>
{
public:
  NavierStokes (const FlowParameters &parameters,
                parallel::distributed::Triangulation<dim> &triangulation,
                TimerOutput                  *external_timer = 0,
                std_cxx11::shared_ptr<helpers::BoundaryDescriptor<dim> > boundary_descriptor =
                  std_cxx11::shared_ptr<helpers::BoundaryDescriptor<dim> >());

  virtual ~NavierStokes();

  std::pair<unsigned int,unsigned int> n_dofs () const;
  void print_n_dofs () const;

  const FiniteElement<dim> &get_fe_u () const;
  const FiniteElement<dim> &get_fe_p () const;

  const DoFHandler<dim>     &get_dof_handler_u () const;
  const ConstraintMatrix    &get_constraints_u () const;
  const DoFHandler<dim>     &get_dof_handler_p () const;
  const ConstraintMatrix    &get_constraints_p () const;

  ConstraintMatrix          &modify_constraints_u ();
  ConstraintMatrix          &modify_constraints_p ();

  void distribute_dofs ();
  void initialize_data_structures ();
  virtual void setup_problem (const Function<dim> &initial_velocity_field,
                              const Function<dim> &initial_distance_function = ZeroFunction<dim>());
  void initialize_matrix_free (MatrixFree<dim> *external_matrix_free = 0);

  void init_time_advance (const bool   print_time_info = true);
  unsigned int evaluate_time_step();
  virtual unsigned int advance_time_step ();

  virtual void output_solution (const std::string output_base_name,
                                const unsigned int n_subdivisions = 0) const;

  /**
   * When solving a problem with boundary conditions that start at a non-zero
   * value but with an initial field that is all zero, one will in general
   * not get a good velocity field. This function can be used to create a
   * divergence-free velocity field by solving the stokes equations with the
   * given boundary values but without any external forces.
   */
  void compute_initial_stokes_field ();

  /**
   * Calls VectorTools::interpolate for the pressure field. Since we might be
   * using FE_Q_DG0 elements where the usual interpolation does not make
   * sense, this class provides a seperate function for it.
   */
  void interpolate_pressure_field (const Function<dim> &pressure_function,
                                   parallel::distributed::Vector<double> &pressure_vector) const;

  void assemble_preconditioner ();

  void build_preconditioner ();

  std::pair<unsigned int,double> solve_system (const double linear_tolerance);

  void vmult(parallel::distributed::BlockVector<double>       &dst,
             const parallel::distributed::BlockVector<double> &src) const;

  void refine_grid_pressure_based (const unsigned int max_grid_level,
                                   const double refine_fraction_of_cells = 0.3,
                                   const double coarsen_fraction_of_cells = 0.05);

  // internally calls triangulation.prepare_coarsening_and_refinement
  void prepare_coarsening_and_refinement ();

  void set_face_average_density(const typename Triangulation<dim>::cell_iterator &cell,
                                const unsigned int face,
                                const double density);

  const FlowParameters   &get_parameters () const;
  const NavierStokesMatrix<dim> &get_matrix() const
  {
    return navier_stokes_matrix;
  }
  NavierStokesMatrix<dim> &get_matrix()
  {
    return navier_stokes_matrix;
  }

  bool get_update_preconditioner() const
  {
    return update_preconditioner;
  }

  // Computes the initial residual of the fluid field, including the part of
  // the residual that does not depend on the time step
  double compute_initial_residual (const bool usual_time_step = true);

  // Solves the nonlinear Navier-Stokes system by a Newton or Newton-like
  // iteration. This function expects that the initial residual is passed into
  // the function as an argument
  unsigned int solve_nonlinear_system (const double initial_residual);

  // return an estimate of the total memory consumption
  std::size_t memory_consumption() const;
  void print_memory_consumption (std::ostream &stream = std::cout) const;

  // vectors that are visible to the user
  parallel::distributed::BlockVector<double> user_rhs;
  parallel::distributed::BlockVector<double> solution, solution_old, solution_old_old, solution_update;

  TimeStepping        time_stepping;

  // it is important to have most of the variables private so that all the
  // changes to internal data structures like the constraint matrix are
  // followed by the correct actions in assembly etc.

private:
  void set_time_step_weight (const double new_weight);
  void apply_boundary_conditions();
  double compute_residual();

  FlowParameters parameters;

  /* MPI_Comm mpi_communicator;
   * probably won't need an extra copy of MPI_Comm in the
   * NavierStokes class as one can always get the communicator
   * easily by triangulation.get_communicator()
   */

  const unsigned int n_mpi_processes;
  const unsigned int this_mpi_process;

  ConditionalOStream  pcout;

  parallel::distributed::Triangulation<dim> &triangulation;


  const FESystem<dim>       fe_u;
  const FESystem<dim>       fe_p;

  DoFHandler<dim>           dof_handler_u;
  DoFHandler<dim>           dof_handler_p;

  ConstraintMatrix          hanging_node_constraints_u;
  ConstraintMatrix          hanging_node_constraints_p;
  ConstraintMatrix          constraints_u;
  ConstraintMatrix          constraints_p;

  NavierStokesMatrix<dim>   navier_stokes_matrix;
  parallel::distributed::BlockVector<double> system_rhs, const_rhs;

  std_cxx11::shared_ptr<parallel::distributed::SolutionTransfer<dim,parallel::distributed::Vector<double> > > sol_trans_u;
  std_cxx11::shared_ptr<parallel::distributed::SolutionTransfer<dim,parallel::distributed::Vector<double> > > sol_trans_p;

  NavierStokesPreconditioner<dim>  preconditioner;

  GrowingVectorMemory<parallel::distributed::BlockVector<double> > solver_memory;

  // here we store the MatrixFree that we
  // use for most of the vector assembly
  // functions and the matrix-free
  // implementation matrix-vector
  // products. There are two possible usages:
  // either we own the matrix_free by
  // ourselves (when calling the function
  // setup()) without argument, or we get it
  // from outside and share it.
  std_cxx11::shared_ptr<MatrixFree<dim> > matrix_free;

  bool                dofs_distributed;
  bool                system_is_setup;

  unsigned int        n_iterations_last_prec_update;
  unsigned int        time_step_last_prec_update;
  bool                update_preconditioner;
  unsigned int        update_preconditioner_frequency;

  std_cxx11::shared_ptr<TimerOutput> timer;
  std::pair<unsigned int,double> solver_timers[2];
};



namespace helpers
{
  // this struct is used to make std::shared_ptr not delete a structure when
  // we create it from a pointer to an external field
  template <typename CLASS>
  struct DummyDeleter
  {
    DummyDeleter (const bool do_delete = false)
      :
      do_delete(do_delete)
    {}

    void operator () (CLASS *pointer)
    {
      if (do_delete)
        delete pointer;
    }

    const bool do_delete;
  };
}


/* ---------------------------- Inline functions ------------------------- */



template <int dim>
inline
const FiniteElement<dim> &NavierStokes<dim>::get_fe_u () const
{
  return fe_u;
}



template <int dim>
inline
const FiniteElement<dim> &NavierStokes<dim>::get_fe_p () const
{
  // We get simpler code by using FESystem, but we want to pretend we have a
  // usual element.
  return fe_p.base_element(0);
}



template <int dim>
inline
const DoFHandler<dim> &NavierStokes<dim>::get_dof_handler_u () const
{
  return dof_handler_u;
}



template <int dim>
inline
const DoFHandler<dim> &NavierStokes<dim>::get_dof_handler_p () const
{
  return dof_handler_p;
}



template <int dim>
inline
const ConstraintMatrix &NavierStokes<dim>::get_constraints_u() const
{
  return constraints_u;
}



template <int dim>
inline
ConstraintMatrix &NavierStokes<dim>::modify_constraints_u()
{
  return constraints_u;
}



template <int dim>
inline
const ConstraintMatrix &NavierStokes<dim>::get_constraints_p() const
{
  return constraints_p;
}



template <int dim>
inline
ConstraintMatrix &NavierStokes<dim>::modify_constraints_p()
{
  return constraints_p;
}



template <int dim>
inline
const FlowParameters &
NavierStokes<dim>::get_parameters () const
{
  return parameters;
}



template <int dim>
inline
void
NavierStokes<dim>::set_face_average_density(const typename Triangulation<dim>::cell_iterator &cell,
                                            const unsigned int face,
                                            const double density)
{
  preconditioner.set_face_average_density(cell, face, density);
}


#endif
