/* ----------------------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under
   the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

#ifdef COMPUTE_CLASS

ComputeStyle(pressure,ComputePressure)

#else

#ifndef LMP_COMPUTE_PRESSURE_H
#define LMP_COMPUTE_PRESSURE_H

#include "compute.h"

namespace LAMMPS_NS {

class ComputePressure : public Compute {
 public:
  ComputePressure(class LAMMPS *, int, char **);
  virtual ~ComputePressure();
  void init();
  double compute_scalar();
  void compute_vector();
  void reset_extra_compute_fix(const char *);

 protected:
  double boltz,nktv2p,inv_volume;
  int nvirial,dimension;
  int nvirialrad; // add rad
  int nvirial_ub; // add rad
  int nvirial_lb; // add rad
  double **vptr;
  double **vptrrad; // add rad
  double **vptr_ub; // add rad
  double **vptr_lb; // add rad
  double *kspace_virial;
  Compute *temperature;
  char *id_temp;
  double virial[6];
  double virialrad[6]; // add rad
  double virial_lb[6]; // add rad
  double virial_ub[6]; // add rad
  int keflag,pairflag,bondflag,angleflag,dihedralflag,improperflag;
  int fixflag,kspaceflag;

  void virial_compute(int, int);
};

}

#endif
#endif

/* ERROR/WARNING messages:

E: Illegal ... command

Self-explanatory.  Check the input script syntax and compare to the
documentation for the command.  You can use -echo screen as a
command-line option when running LAMMPS to see the offending line.

E: Compute pressure must use group all

Virial contributions computed by potentials (pair, bond, etc) are
computed on all atoms.

E: Could not find compute pressure temperature ID

The compute ID for calculating temperature does not exist.

E: Compute pressure temperature ID does not compute temperature

The compute ID assigned to a pressure computation must compute
temperature.

E: Must use 'kspace_modify pressure/scalar no' to get individual
components of pressure tensor with kspace_style MSM

Self-explanatory.

E: Virial was not tallied on needed timestep

You are using a thermo keyword that requires potentials to
have tallied the virial, but they didn't on this timestep.  See the
variable doc page for ideas on how to make this work.

*/