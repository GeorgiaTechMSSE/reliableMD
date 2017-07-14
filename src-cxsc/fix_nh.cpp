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

/* ----------------------------------------------------------------------
   Contributing authors: Mark Stevens (SNL), Aidan Thompson (SNL)
------------------------------------------------------------------------- */

#include "string.h"
#include "stdlib.h"
#include "math.h"
#include "fix_nh.h"
#include "math_extra.h"
#include "atom.h"
#include "force.h"
#include "group.h"
#include "comm.h"
#include "irregular.h"
#include "modify.h"
#include "fix_deform.h"
#include "compute.h"
#include "kspace.h"
#include "update.h"
#include "respa.h"
#include "domain.h"
#include "memory.h"
#include "error.h"
// AT: add CXSC-lib
// #include "imatrix.hpp" // unnecessary
#include "ivector.hpp"
#include "interval.hpp"

// for debug purpose only
// #include <iostream>
// #include <fstream>

using namespace LAMMPS_NS;
using namespace FixConst;
// using namespace cxsc;

#define DELTAFLIP 0.1
#define TILTMAX 1.5

enum{NOBIAS,BIAS};
enum{NONE,XYZ,XY,YZ,XZ};
enum{ISO,ANISO,TRICLINIC};

/* ----------------------------------------------------------------------
   NVT,NPH,NPT integrators for improved Nose-Hoover equations of motion
 ---------------------------------------------------------------------- */

FixNH::FixNH(LAMMPS *lmp, int narg, char **arg) : Fix(lmp, narg, arg)
{
  if (narg < 4) error->all(FLERR,"Illegal fix nvt/npt/nph command");

  restart_global = 1;
  dynamic_group_allow = 1;
  time_integrate = 1;
  scalar_flag = 1;
  vector_flag = 1;
  global_freq = 1;
  extscalar = 1;
  extvector = 0;

  // default values
  // par = parallel with

  pcouple = NONE;
  drag = 0.0;
  interval_drag = new interval; *interval_drag = interval(0.0);       // par drag
  allremap = 1;
  id_dilate = NULL;
  mtchain = mpchain = 3;
  nc_tchain = nc_pchain = 1;
  mtk_flag = 1;
  deviatoric_flag = 0;
  nreset_h0 = 0;
  eta_mass_flag = 1;
  omega_mass_flag = 0;
  etap_mass_flag = 0;
  flipflag = 1;

  // turn on tilt factor scaling, whenever applicable

  dimension = domain->dimension;

  scaleyz = scalexz = scalexy = 0;
  if (domain->yperiodic && domain->xy != 0.0) scalexy = 1;
  if (domain->zperiodic && dimension == 3) {
    if (domain->yz != 0.0) scaleyz = 1;
    if (domain->xz != 0.0) scalexz = 1;
  }

  // set fixed-point to default = center of cell

  fixedpoint[0] = 0.5*(domain->boxlo[0]+domain->boxhi[0]);
  fixedpoint[1] = 0.5*(domain->boxlo[1]+domain->boxhi[1]);
  fixedpoint[2] = 0.5*(domain->boxlo[2]+domain->boxhi[2]);

  // used by FixNVTSllod to preserve non-default value

  mtchain_default_flag = 1;

  tstat_flag = 0;
  double t_period = 0.0;
  double p_period[6];

  interval_t_start = new interval;    // par t_start
  interval_t_stop = new interval;     // par t_stop
  interval_t_current = new interval;  // par t_current
  interval_t_target = new interval;   // par t_target
  interval_t_period = new interval;   // par t_period
  interval_t_freq = new interval;     // par t_freq
  
  ivector_p_start = new ivector[6];   for (int i = 0; i < 6; i++) Resize(ivector_p_start[i],1);   // par p_start
  ivector_p_stop = new ivector[6];    for (int i = 0; i < 6; i++) Resize(ivector_p_stop[i],1);    // par p_stop
  ivector_p_current = new ivector[6]; for (int i = 0; i < 6; i++) Resize(ivector_p_current[i],1); // par p_current
  ivector_p_target = new ivector[6];  for (int i = 0; i < 6; i++) Resize(ivector_p_target[i],1);  // par p_target
  ivector_p_period = new ivector[6];  for (int i = 0; i < 6; i++) Resize(ivector_p_period[i],1);  // par p_period
  ivector_p_freq = new ivector[6];    for (int i = 0; i < 6; i++) Resize(ivector_p_freq[i],1);    // par p_freq


  for (int i = 0; i < 6; i++) {
    p_start[i] = p_stop[i] = p_period[i] = p_target[i] = 0.0;
    ivector_p_start[i] = ivector_p_stop[i] = ivector_p_period[i] = ivector_p_target[i] = interval(0.0); // par p_period, p_start, p_stop, p_target

    p_flag[i] = 0;
  }

  // process keywords

  int iarg = 3;


  while (iarg < narg) {
    if (strcmp(arg[iarg],"temp") == 0) {
      if (iarg+4 > narg) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      tstat_flag = 1;
      t_start = force->numeric(FLERR,arg[iarg+1]);
      t_target = t_start;
      t_stop = force->numeric(FLERR,arg[iarg+2]);
      t_period = force->numeric(FLERR,arg[iarg+3]);

      *interval_t_start = interval(t_start);
      *interval_t_target = interval(t_target);
      *interval_t_stop = interval(t_stop);
      *interval_t_period = interval(t_period);

      if (t_start < 0.0 || t_stop <= 0.0)
        error->all(FLERR,
                   "Target temperature for fix nvt/npt/nph cannot be 0.0");
      iarg += 4;

    } else if (strcmp(arg[iarg],"iso") == 0) {
      if (iarg+4 > narg) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      pcouple = XYZ;
      p_start[0] = p_start[1] = p_start[2] = force->numeric(FLERR,arg[iarg+1]);
      p_stop[0] = p_stop[1] = p_stop[2] = force->numeric(FLERR,arg[iarg+2]);
      p_period[0] = p_period[1] = p_period[2] = force->numeric(FLERR,arg[iarg+3]);

      ivector_p_start[0] = ivector_p_start[1] = ivector_p_start[2] = interval(force->numeric(FLERR,arg[iarg+1]));     // par p_start
      ivector_p_stop[0] = ivector_p_stop[1] = ivector_p_stop[2] = interval(force->numeric(FLERR,arg[iarg+2]));        // par p_stop
      ivector_p_period[0] = ivector_p_period[1] = ivector_p_period[2] = interval(force->numeric(FLERR,arg[iarg+3]));  // par p_period

      p_flag[0] = p_flag[1] = p_flag[2] = 1;
      if (dimension == 2) {
        p_start[2] = p_stop[2] = p_period[2] = 0.0;
        ivector_p_start[2] = ivector_p_stop[2] = ivector_p_period[2] = interval(0.0);

        p_flag[2] = 0;
      }
      iarg += 4;
    } else if (strcmp(arg[iarg],"aniso") == 0) {
      if (iarg+4 > narg) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      pcouple = NONE;
      p_start[0] = p_start[1] = p_start[2] = force->numeric(FLERR,arg[iarg+1]);
      p_stop[0] = p_stop[1] = p_stop[2] = force->numeric(FLERR,arg[iarg+2]);
      p_period[0] = p_period[1] = p_period[2] = force->numeric(FLERR,arg[iarg+3]);

      ivector_p_start[0] = ivector_p_start[1] = ivector_p_start[2] = interval(force->numeric(FLERR,arg[iarg+1]));
      ivector_p_stop[0] = ivector_p_stop[1] = ivector_p_stop[2] = interval(force->numeric(FLERR,arg[iarg+2]));
      ivector_p_period[0] = ivector_p_period[1] = ivector_p_period[2] = interval(force->numeric(FLERR,arg[iarg+3]));

      p_flag[0] = p_flag[1] = p_flag[2] = 1;
      if (dimension == 2) {
        p_start[2] = p_stop[2] = p_period[2] = 0.0;
        ivector_p_start[2] = ivector_p_stop[2] = ivector_p_period[2] = interval(0.0);

        p_flag[2] = 0;
      }
      iarg += 4;
    } else if (strcmp(arg[iarg],"tri") == 0) {
      if (iarg+4 > narg) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      pcouple = NONE;
      scalexy = scalexz = scaleyz = 0;
      p_start[0] = p_start[1] = p_start[2] = force->numeric(FLERR,arg[iarg+1]);
      p_stop[0] = p_stop[1] = p_stop[2] = force->numeric(FLERR,arg[iarg+2]);
      p_period[0] = p_period[1] = p_period[2] = force->numeric(FLERR,arg[iarg+3]);

      ivector_p_start[0] = ivector_p_start[1] = ivector_p_start[2] = interval(force->numeric(FLERR,arg[iarg+1]));
      ivector_p_stop[0] = ivector_p_stop[1] = ivector_p_stop[2] = interval(force->numeric(FLERR,arg[iarg+2]));
      ivector_p_period[0] = ivector_p_period[1] = ivector_p_period[2] = interval(force->numeric(FLERR,arg[iarg+3]));

      p_flag[0] = p_flag[1] = p_flag[2] = 1;

      p_start[3] = p_start[4] = p_start[5] = 0.0;
      p_stop[3] = p_stop[4] = p_stop[5] = 0.0;
      p_period[3] = p_period[4] = p_period[5] = force->numeric(FLERR,arg[iarg+3]);

      ivector_p_start[3] = ivector_p_start[4] = ivector_p_start[5] = interval(0.0);
      ivector_p_stop[3] = ivector_p_stop[4] = ivector_p_stop[5] = interval(0.0);
      ivector_p_period[3] = ivector_p_period[4] = ivector_p_period[5] = interval(force->numeric(FLERR,arg[iarg+3]));

      p_flag[3] = p_flag[4] = p_flag[5] = 1;
      if (dimension == 2) {
        p_start[2] = p_stop[2] = p_period[2] = 0.0;
        ivector_p_start[2] = ivector_p_stop[2] = ivector_p_period[2] = interval(0.0);
        p_flag[2] = 0;
        p_start[3] = p_stop[3] = p_period[3] = 0.0;
        ivector_p_start[3] = ivector_p_stop[3] = ivector_p_period[3] = interval(0.0);
        p_flag[3] = 0;
        p_start[4] = p_stop[4] = p_period[4] = 0.0;
        ivector_p_start[4] = ivector_p_stop[4] = ivector_p_period[4] = interval(0.0);
        p_flag[4] = 0;
      }
      iarg += 4;
    } else if (strcmp(arg[iarg],"x") == 0) {
      if (iarg+4 > narg) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      p_start[0] = force->numeric(FLERR,arg[iarg+1]);
      p_stop[0] = force->numeric(FLERR,arg[iarg+2]);
      p_period[0] = force->numeric(FLERR,arg[iarg+3]);

      ivector_p_start[0] = interval(p_start[0]);
      ivector_p_stop[0] = interval(p_stop[0]);
      ivector_p_period[0] = interval(p_period[0]);

      p_flag[0] = 1;
      deviatoric_flag = 1;
      iarg += 4;
    } else if (strcmp(arg[iarg],"y") == 0) {
      if (iarg+4 > narg) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      p_start[1] = force->numeric(FLERR,arg[iarg+1]);
      p_stop[1] = force->numeric(FLERR,arg[iarg+2]);
      p_period[1] = force->numeric(FLERR,arg[iarg+3]);

      ivector_p_start[1] = interval(p_start[1]);
      ivector_p_stop[1] = interval(p_stop[1]);
      ivector_p_period[1] = interval(p_period[1]);
      
      p_flag[1] = 1;
      deviatoric_flag = 1;
      iarg += 4;
    } else if (strcmp(arg[iarg],"z") == 0) {
      if (iarg+4 > narg) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      p_start[2] = force->numeric(FLERR,arg[iarg+1]);
      p_stop[2] = force->numeric(FLERR,arg[iarg+2]);
      p_period[2] = force->numeric(FLERR,arg[iarg+3]);

      ivector_p_start[2] = interval(p_start[2]);
      ivector_p_stop[2] = interval(p_stop[2]);
      ivector_p_period[2] = interval(p_period[2]);

      p_flag[2] = 1;
      deviatoric_flag = 1;
      iarg += 4;
      if (dimension == 2)
        error->all(FLERR,"Invalid fix nvt/npt/nph command for a 2d simulation");

    } else if (strcmp(arg[iarg],"yz") == 0) {
      if (iarg+4 > narg) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      p_start[3] = force->numeric(FLERR,arg[iarg+1]);
      p_stop[3] = force->numeric(FLERR,arg[iarg+2]);
      p_period[3] = force->numeric(FLERR,arg[iarg+3]);

      ivector_p_start[3] = interval(p_start[3]);
      ivector_p_stop[3] = interval(p_stop[3]);
      ivector_p_period[3] = interval(p_period[3]);

      p_flag[3] = 1;
      deviatoric_flag = 1;
      scaleyz = 0;
      iarg += 4;
      if (dimension == 2)
        error->all(FLERR,"Invalid fix nvt/npt/nph command for a 2d simulation");
    } else if (strcmp(arg[iarg],"xz") == 0) {
      if (iarg+4 > narg) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      p_start[4] = force->numeric(FLERR,arg[iarg+1]);
      p_stop[4] = force->numeric(FLERR,arg[iarg+2]);
      p_period[4] = force->numeric(FLERR,arg[iarg+3]);

      ivector_p_start[4] = interval(p_start[4]);
      ivector_p_stop[4] = interval(p_stop[4]);
      ivector_p_period[4] = interval(p_period[4]);

      p_flag[4] = 1;
      deviatoric_flag = 1;
      scalexz = 0;
      iarg += 4;
      if (dimension == 2)
        error->all(FLERR,"Invalid fix nvt/npt/nph command for a 2d simulation");
    } else if (strcmp(arg[iarg],"xy") == 0) {
      if (iarg+4 > narg) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      p_start[5] = force->numeric(FLERR,arg[iarg+1]);
      p_stop[5] = force->numeric(FLERR,arg[iarg+2]);
      p_period[5] = force->numeric(FLERR,arg[iarg+3]);

      ivector_p_start[5] = interval(p_start[5]);
      ivector_p_stop[5] = interval(p_stop[5]);
      ivector_p_period[5] = interval(p_period[5]);

      p_flag[5] = 1;
      deviatoric_flag = 1;
      scalexy = 0;
      iarg += 4;

    } else if (strcmp(arg[iarg],"couple") == 0) {
      if (iarg+2 > narg) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      if (strcmp(arg[iarg+1],"xyz") == 0) pcouple = XYZ;
      else if (strcmp(arg[iarg+1],"xy") == 0) pcouple = XY;
      else if (strcmp(arg[iarg+1],"yz") == 0) pcouple = YZ;
      else if (strcmp(arg[iarg+1],"xz") == 0) pcouple = XZ;
      else if (strcmp(arg[iarg+1],"none") == 0) pcouple = NONE;
      else error->all(FLERR,"Illegal fix nvt/npt/nph command");
      iarg += 2;

    } else if (strcmp(arg[iarg],"drag") == 0) {
      if (iarg+2 > narg) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      drag = force->numeric(FLERR,arg[iarg+1]);
      if (drag < 0.0) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      iarg += 2;
    } else if (strcmp(arg[iarg],"dilate") == 0) {
      if (iarg+2 > narg) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      if (strcmp(arg[iarg+1],"all") == 0) allremap = 1;
      else {
        allremap = 0;
        delete [] id_dilate;
        int n = strlen(arg[iarg+1]) + 1;
        id_dilate = new char[n];
        strcpy(id_dilate,arg[iarg+1]);
        int idilate = group->find(id_dilate);
        if (idilate == -1)
          error->all(FLERR,"Fix nvt/npt/nph dilate group ID does not exist");
      }
      iarg += 2;

    } else if (strcmp(arg[iarg],"tchain") == 0) {
      if (iarg+2 > narg) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      mtchain = force->inumeric(FLERR,arg[iarg+1]);
      // used by FixNVTSllod to preserve non-default value
      mtchain_default_flag = 0;
      if (mtchain < 1) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      iarg += 2;
    } else if (strcmp(arg[iarg],"pchain") == 0) {
      if (iarg+2 > narg) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      mpchain = force->inumeric(FLERR,arg[iarg+1]);
      if (mpchain < 0) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      iarg += 2;
    } else if (strcmp(arg[iarg],"mtk") == 0) {
      if (iarg+2 > narg) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      if (strcmp(arg[iarg+1],"yes") == 0) mtk_flag = 1;
      else if (strcmp(arg[iarg+1],"no") == 0) mtk_flag = 0;
      else error->all(FLERR,"Illegal fix nvt/npt/nph command");
      iarg += 2;
    } else if (strcmp(arg[iarg],"tloop") == 0) {
      if (iarg+2 > narg) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      nc_tchain = force->inumeric(FLERR,arg[iarg+1]);
      if (nc_tchain < 0) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      iarg += 2;
    } else if (strcmp(arg[iarg],"ploop") == 0) {
      if (iarg+2 > narg) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      nc_pchain = force->inumeric(FLERR,arg[iarg+1]);
      if (nc_pchain < 0) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      iarg += 2;
    } else if (strcmp(arg[iarg],"nreset") == 0) {
      if (iarg+2 > narg) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      nreset_h0 = force->inumeric(FLERR,arg[iarg+1]);
      if (nreset_h0 < 0) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      iarg += 2;
    } else if (strcmp(arg[iarg],"scalexy") == 0) {
      if (iarg+2 > narg) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      if (strcmp(arg[iarg+1],"yes") == 0) scalexy = 1;
      else if (strcmp(arg[iarg+1],"no") == 0) scalexy = 0;
      else error->all(FLERR,"Illegal fix nvt/npt/nph command");
      iarg += 2;
    } else if (strcmp(arg[iarg],"scalexz") == 0) {
      if (iarg+2 > narg) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      if (strcmp(arg[iarg+1],"yes") == 0) scalexz = 1;
      else if (strcmp(arg[iarg+1],"no") == 0) scalexz = 0;
      else error->all(FLERR,"Illegal fix nvt/npt/nph command");
      iarg += 2;
    } else if (strcmp(arg[iarg],"scaleyz") == 0) {
      if (iarg+2 > narg) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      if (strcmp(arg[iarg+1],"yes") == 0) scaleyz = 1;
      else if (strcmp(arg[iarg+1],"no") == 0) scaleyz = 0;
      else error->all(FLERR,"Illegal fix nvt/npt/nph command");
      iarg += 2;
    } else if (strcmp(arg[iarg],"flip") == 0) {
      if (iarg+2 > narg) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      if (strcmp(arg[iarg+1],"yes") == 0) flipflag = 1;
      else if (strcmp(arg[iarg+1],"no") == 0) flipflag = 0;
      else error->all(FLERR,"Illegal fix nvt/npt/nph command");
      iarg += 2;
    } else if (strcmp(arg[iarg],"fixedpoint") == 0) {
      if (iarg+4 > narg) error->all(FLERR,"Illegal fix nvt/npt/nph command");
      fixedpoint[0] = force->numeric(FLERR,arg[iarg+1]);
      fixedpoint[1] = force->numeric(FLERR,arg[iarg+2]);
      fixedpoint[2] = force->numeric(FLERR,arg[iarg+3]);

      iarg += 4;
    } else error->all(FLERR,"Illegal fix nvt/npt/nph command");
  }

  // error checks

  if (dimension == 2 && (p_flag[2] || p_flag[3] || p_flag[4]))
    error->all(FLERR,"Invalid fix nvt/npt/nph command for a 2d simulation");
  if (dimension == 2 && (pcouple == YZ || pcouple == XZ))
    error->all(FLERR,"Invalid fix nvt/npt/nph command for a 2d simulation");
  if (dimension == 2 && (scalexz == 1 || scaleyz == 1 ))
    error->all(FLERR,"Invalid fix nvt/npt/nph command for a 2d simulation");

  if (pcouple == XYZ && (p_flag[0] == 0 || p_flag[1] == 0))
    error->all(FLERR,"Invalid fix nvt/npt/nph command pressure settings");
  if (pcouple == XYZ && dimension == 3 && p_flag[2] == 0)
    error->all(FLERR,"Invalid fix nvt/npt/nph command pressure settings");
  if (pcouple == XY && (p_flag[0] == 0 || p_flag[1] == 0))
    error->all(FLERR,"Invalid fix nvt/npt/nph command pressure settings");
  if (pcouple == YZ && (p_flag[1] == 0 || p_flag[2] == 0))
    error->all(FLERR,"Invalid fix nvt/npt/nph command pressure settings");
  if (pcouple == XZ && (p_flag[0] == 0 || p_flag[2] == 0))
    error->all(FLERR,"Invalid fix nvt/npt/nph command pressure settings");

  // require periodicity in tensile dimension

  if (p_flag[0] && domain->xperiodic == 0)
    error->all(FLERR,"Cannot use fix nvt/npt/nph on a non-periodic dimension");
  if (p_flag[1] && domain->yperiodic == 0)
    error->all(FLERR,"Cannot use fix nvt/npt/nph on a non-periodic dimension");
  if (p_flag[2] && domain->zperiodic == 0)
    error->all(FLERR,"Cannot use fix nvt/npt/nph on a non-periodic dimension");

  // require periodicity in 2nd dim of off-diagonal tilt component

  if (p_flag[3] && domain->zperiodic == 0)
    error->all(FLERR,
               "Cannot use fix nvt/npt/nph on a 2nd non-periodic dimension");
  if (p_flag[4] && domain->zperiodic == 0)
    error->all(FLERR,
               "Cannot use fix nvt/npt/nph on a 2nd non-periodic dimension");
  if (p_flag[5] && domain->yperiodic == 0)
    error->all(FLERR,
               "Cannot use fix nvt/npt/nph on a 2nd non-periodic dimension");

  if (scaleyz == 1 && domain->zperiodic == 0)
    error->all(FLERR,"Cannot use fix nvt/npt/nph "
               "with yz scaling when z is non-periodic dimension");
  if (scalexz == 1 && domain->zperiodic == 0)
    error->all(FLERR,"Cannot use fix nvt/npt/nph "
               "with xz scaling when z is non-periodic dimension");
  if (scalexy == 1 && domain->yperiodic == 0)
    error->all(FLERR,"Cannot use fix nvt/npt/nph "
               "with xy scaling when y is non-periodic dimension");

  if (p_flag[3] && scaleyz == 1)
    error->all(FLERR,"Cannot use fix nvt/npt/nph with "
               "both yz dynamics and yz scaling");
  if (p_flag[4] && scalexz == 1)
    error->all(FLERR,"Cannot use fix nvt/npt/nph with "
               "both xz dynamics and xz scaling");
  if (p_flag[5] && scalexy == 1)
    error->all(FLERR,"Cannot use fix nvt/npt/nph with "
               "both xy dynamics and xy scaling");

  if (!domain->triclinic && (p_flag[3] || p_flag[4] || p_flag[5]))
    error->all(FLERR,"Can not specify Pxy/Pxz/Pyz in "
               "fix nvt/npt/nph with non-triclinic box");

  if (pcouple == XYZ && dimension == 3 &&
      (p_start[0] != p_start[1] || p_start[0] != p_start[2] ||
       p_stop[0] != p_stop[1] || p_stop[0] != p_stop[2] ||
       p_period[0] != p_period[1] || p_period[0] != p_period[2]))
    error->all(FLERR,"Invalid fix nvt/npt/nph pressure settings");
  if (pcouple == XYZ && dimension == 2 &&
      (p_start[0] != p_start[1] || p_stop[0] != p_stop[1] ||
       p_period[0] != p_period[1]))
    error->all(FLERR,"Invalid fix nvt/npt/nph pressure settings");
  if (pcouple == XY &&
      (p_start[0] != p_start[1] || p_stop[0] != p_stop[1] ||
       p_period[0] != p_period[1]))
    error->all(FLERR,"Invalid fix nvt/npt/nph pressure settings");
  if (pcouple == YZ &&
      (p_start[1] != p_start[2] || p_stop[1] != p_stop[2] ||
       p_period[1] != p_period[2]))
    error->all(FLERR,"Invalid fix nvt/npt/nph pressure settings");
  if (pcouple == XZ &&
      (p_start[0] != p_start[2] || p_stop[0] != p_stop[2] ||
       p_period[0] != p_period[2]))
    error->all(FLERR,"Invalid fix nvt/npt/nph pressure settings");

  if ((tstat_flag && t_period <= 0.0) ||
      (p_flag[0] && p_period[0] <= 0.0) ||
      (p_flag[1] && p_period[1] <= 0.0) ||
      (p_flag[2] && p_period[2] <= 0.0) ||
      (p_flag[3] && p_period[3] <= 0.0) ||
      (p_flag[4] && p_period[4] <= 0.0) ||
      (p_flag[5] && p_period[5] <= 0.0))
    error->all(FLERR,"Fix nvt/npt/nph damping parameters must be > 0.0");

  // set pstat_flag and box change and restart_pbc variables

  pstat_flag = 0;
  for (int i = 0; i < 6; i++)
    if (p_flag[i]) pstat_flag = 1;

  if (pstat_flag) {
    if (p_flag[0] || p_flag[1] || p_flag[2]) box_change_size = 1;
    if (p_flag[3] || p_flag[4] || p_flag[5]) box_change_shape = 1;
    no_change_box = 1;
    if (allremap == 0) restart_pbc = 1;
  }

  // pstyle = TRICLINIC if any off-diagonal term is controlled -> 6 dof
  // else pstyle = ISO if XYZ coupling or XY coupling in 2d -> 1 dof
  // else pstyle = ANISO -> 3 dof

  if (p_flag[3] || p_flag[4] || p_flag[5]) pstyle = TRICLINIC;
  else if (pcouple == XYZ || (dimension == 2 && pcouple == XY)) pstyle = ISO;
  else pstyle = ANISO;

  // pre_exchange only required if flips can occur due to shape changes

  pre_exchange_flag = 0;
  if (flipflag && (p_flag[3] || p_flag[4] || p_flag[5])) pre_exchange_flag = 1;
  if (flipflag && (domain->yz != 0.0 || domain->xz != 0.0 || domain->xy != 0.0))
    pre_exchange_flag = 1;

  // convert input periods to frequencies

  t_freq = 0.0;
  p_freq[0] = p_freq[1] = p_freq[2] = p_freq[3] = p_freq[4] = p_freq[5] = 0.0;

  *interval_t_freq = interval(0.0);
  ivector_p_freq[0] = ivector_p_freq[1] = ivector_p_freq[2] = ivector_p_freq[3] = ivector_p_freq[4] = ivector_p_freq[5] = interval(0.0);

  // ORIG:
  // if (tstat_flag) t_freq = 1.0 / t_period;
  // if (p_flag[0]) p_freq[0] = 1.0 / p_period[0];
  // if (p_flag[1]) p_freq[1] = 1.0 / p_period[1];
  // if (p_flag[2]) p_freq[2] = 1.0 / p_period[2];
  // if (p_flag[3]) p_freq[3] = 1.0 / p_period[3];
  // if (p_flag[4]) p_freq[4] = 1.0 / p_period[4];
  // if (p_flag[5]) p_freq[5] = 1.0 / p_period[5];

  // MOD:
  if (tstat_flag) { t_freq = 1.0 / t_period; *interval_t_freq = 1.0 / (*interval_t_period); }
  if (p_flag[0]) { p_freq[0] = 1.0 / p_period[0]; ivector_p_freq[0] = 1.0 / interval(ivector_p_period[0]); }
  if (p_flag[1]) { p_freq[1] = 1.0 / p_period[1]; ivector_p_freq[1] = 1.0 / interval(ivector_p_period[1]); }
  if (p_flag[2]) { p_freq[2] = 1.0 / p_period[2]; ivector_p_freq[2] = 1.0 / interval(ivector_p_period[2]); }
  if (p_flag[3]) { p_freq[3] = 1.0 / p_period[3]; ivector_p_freq[3] = 1.0 / interval(ivector_p_period[3]); }
  if (p_flag[4]) { p_freq[4] = 1.0 / p_period[4]; ivector_p_freq[4] = 1.0 / interval(ivector_p_period[4]); }
  if (p_flag[5]) { p_freq[5] = 1.0 / p_period[5]; ivector_p_freq[5] = 1.0 / interval(ivector_p_period[5]); }


  // Nose/Hoover temp and pressure init

  size_vector = 0;

  if (tstat_flag) {
    int ich;
    eta = new double[mtchain];
    ivector_eta = new ivector[mtchain]; for (int i = 0; i < mtchain; i++) Resize(ivector_eta[i],1);   // par eta

    // add one extra dummy thermostat, set to zero

    eta_dot = new double[mtchain+1];
    ivector_eta_dot = new ivector[mtchain+1]; for (int i = 0; i < mtchain+1; i++) Resize(ivector_eta_dot[i],1);     // par eta_dot

    eta_dot[mtchain] = 0.0;
    ivector_eta_dot[mtchain] = interval(0.0);

    eta_dotdot = new double[mtchain];
    ivector_eta_dotdot = new ivector[mtchain]; for (int i = 0; i < mtchain; i++) Resize(ivector_eta_dotdot[i],1);   // par eta_dotdot

    for (ich = 0; ich < mtchain; ich++) {
      eta[ich] = eta_dot[ich] = eta_dotdot[ich] = 0.0;
      ivector_eta[ich] = ivector_eta_dot[ich] = ivector_eta_dotdot[ich] = interval(0.0);
    }

    eta_mass = new double[mtchain];
    ivector_eta_mass = new ivector[mtchain]; for (int i = 0; i < mtchain; i++) Resize(ivector_eta_mass[i],1);       // par eta_mass

    size_vector += 2*2*mtchain;
  }

  if (pstat_flag) {
    // init ivector before computing
    ivector_omega = new ivector[6]; 
    ivector_omega_dot = new ivector[6]; 
    ivector_omega_mass = new ivector[6]; 
    for (int i = 0; i < 6; i++) {
      Resize(ivector_omega[i],1);           // par omega
      Resize(ivector_omega_dot[i],1);       // par omega_dot
      Resize(ivector_omega_mass[i],1);      // par omega_mass
    }

    omega[0] = omega[1] = omega[2] = 0.0;
    ivector_omega[0] = ivector_omega[1] = ivector_omega[2] = interval(0.0);

    omega_dot[0] = omega_dot[1] = omega_dot[2] = 0.0;
    ivector_omega_dot[0] = ivector_omega_dot[1] = ivector_omega_dot[2] = interval(0.0);

    omega_mass[0] = omega_mass[1] = omega_mass[2] = 0.0;
    ivector_omega_mass[0] = ivector_omega_mass[1] = ivector_omega_mass[2] = interval(0.0);

    omega[3] = omega[4] = omega[5] = 0.0;
    ivector_omega[3] = ivector_omega[4] = ivector_omega[5] = interval(0.0);

    omega_dot[3] = omega_dot[4] = omega_dot[5] = 0.0;
    ivector_omega_dot[3] = ivector_omega_dot[4] = ivector_omega_dot[5] = interval(0.0);

    omega_mass[3] = omega_mass[4] = omega_mass[5] = 0.0;
    ivector_omega_mass[3] = ivector_omega_mass[4] = ivector_omega_mass[5] = interval(0.0);

    if (pstyle == ISO) size_vector += 2*2*1;
    else if (pstyle == ANISO) size_vector += 2*2*3;
    else if (pstyle == TRICLINIC) size_vector += 2*2*6;

    if (mpchain) {
      int ich;
      etap = new double[mpchain];
      ivector_etap = new ivector[mpchain]; for (int i = 0 ; i < mpchain; i++) Resize(ivector_etap[i],1);

      // add one extra dummy thermostat, set to zero

      etap_dot = new double[mpchain+1];
      ivector_etap_dot = new ivector[mpchain+1]; for (int i = 0; i < mpchain+1; i++) Resize(ivector_etap_dot[i],1);

      etap_dot[mpchain] = 0.0;
      ivector_etap_dot[mpchain] = interval(0.0); 

      etap_dotdot = new double[mpchain];
      ivector_etap_dotdot = new ivector[mpchain]; for (int i = 0; i < mpchain; i++) Resize(ivector_etap_dotdot[i],1); 

      for (ich = 0; ich < mpchain; ich++) {
        etap[ich] = etap_dot[ich] = etap_dotdot[ich] = 0.0;
        ivector_etap[ich] = ivector_etap_dot[ich] = ivector_etap_dotdot[ich] = interval(0.0);
      }
      etap_mass = new double[mpchain];
      ivector_etap_mass = new ivector[mpchain]; for (int i = 0; i < mpchain; i++) Resize(ivector_etap_mass[i],1);

      size_vector += 2*2*mpchain;
    }

    if (deviatoric_flag) size_vector += 1;
  }

  nrigid = 0;
  rfix = NULL;

  if (pre_exchange_flag) irregular = new Irregular(lmp);
  else irregular = NULL;

  // initialize vol0,t0 to zero to signal uninitialized
  // values then assigned in init(), if necessary

  vol0 = t0 = 0.0;

  // add declarations of dynamically-allocated variables
  interval_pdrag_factor = new interval;
  interval_tdrag_factor = new interval;

  // ivector_h0_inv = new ivector[6]; for (int i = 0; i < 6; i++) Resize(ivector_h0_inv[i],1);   // par h0_inv

  interval_kecurrent = new interval; 
  interval_expfac = new interval;
  interval_factor_etap = new interval;

  interval_ke_target = new interval;

  interval_factor_eta = new interval;

  interval_p_hydro = new interval;

  ivector_sigma = new ivector[6]; for (int i = 0; i < 6; i++) Resize(ivector_sigma[i],1); 

  interval_f_omega = new interval;

  ivector_h = new ivector[6]; for (int i = 0; i < 6; i++) Resize(ivector_h[i],1); 
  ivector_fdev = new ivector[6]; for (int i = 0; i < 6; i++) Resize(ivector_fdev[i],1); 

  interval_mtk_term1 = new interval;
  interval_mtk_term2 = new interval;

  ivector_factor = new ivector[3]; for (int i = 0; i < 3; i++) Resize(ivector_factor[i],1);

  ivector_tensor = new ivector[6]; for (int i = 0; i < 6; i++) Resize(ivector_tensor[i],1);

  std::cout << SetPrecision(20,15); // tune accuracy of interval arithmetic
  sync_step = new int;
  *sync_step = 50;
}

/* ---------------------------------------------------------------------- */

FixNH::~FixNH()
{
  delete [] id_dilate;
  delete [] rfix;

  delete irregular;

  // delete temperature and pressure if fix created them

  if (tflag) modify->delete_compute(id_temp);
  delete [] id_temp;

  if (tstat_flag) {
    delete [] eta;
    delete [] eta_dot;
    delete [] eta_dotdot;
    delete [] eta_mass;

    delete [] ivector_eta;
    delete [] ivector_eta_dot;
    delete [] ivector_eta_dotdot;
    delete [] ivector_eta_mass;    
  }

  if (pstat_flag) {
    if (pflag) modify->delete_compute(id_press);
    delete [] id_press;

    delete [] ivector_omega;
    delete [] ivector_omega_mass;
    delete [] ivector_omega_dot;

    if (mpchain) {
      delete [] etap;
      delete [] etap_dot;
      delete [] etap_dotdot;
      delete [] etap_mass;

      delete [] ivector_etap;
      delete [] ivector_etap_dot;
      delete [] ivector_etap_dotdot;
      delete [] ivector_etap_mass;       
    }
  }

  // delete dynamically-allocated interval/ivector objects - editted by AT
  delete interval_drag;

  delete interval_t_start;
  delete interval_t_stop;
  delete interval_t_current;
  delete interval_t_target;
  delete interval_t_period;
  delete interval_t_freq;

  delete [] ivector_p_start;
  delete [] ivector_p_stop;
  delete [] ivector_p_current;
  delete [] ivector_p_target;
  delete [] ivector_p_period;
  delete [] ivector_p_freq;

  // delete [] ivector_h0_inv; // dilating volume = useless
  delete interval_tdrag_factor;
  delete interval_pdrag_factor;

  // initial_integrate - final_integrate
  delete interval_kecurrent;
  delete interval_expfac;
  delete interval_factor_etap;

  delete interval_ke_target;

  delete interval_factor_eta;

  delete interval_p_hydro;

  delete [] ivector_sigma;

  delete interval_f_omega;

  delete [] ivector_h;

  delete [] ivector_fdev;

  delete interval_mtk_term1;
  delete interval_mtk_term2;

  delete [] ivector_factor;

  delete [] ivector_tensor;

  delete sync_step;
}

/* ---------------------------------------------------------------------- */

int FixNH::setmask()
{
  int mask = 0;
  mask |= INITIAL_INTEGRATE;
  mask |= FINAL_INTEGRATE;
  mask |= THERMO_ENERGY;
  mask |= INITIAL_INTEGRATE_RESPA;
  mask |= FINAL_INTEGRATE_RESPA;
  if (pre_exchange_flag) mask |= PRE_EXCHANGE;
  return mask;
}

/* ---------------------------------------------------------------------- */

void FixNH::init()
{
  // recheck that dilate group has not been deleted

  if (allremap == 0) {
    int idilate = group->find(id_dilate);
    if (idilate == -1)
      error->all(FLERR,"Fix nvt/npt/nph dilate group ID does not exist");
    dilate_group_bit = group->bitmask[idilate];
  }

  // ensure no conflict with fix deform

  if (pstat_flag)
    for (int i = 0; i < modify->nfix; i++)
      if (strcmp(modify->fix[i]->style,"deform") == 0) {
        int *dimflag = ((FixDeform *) modify->fix[i])->dimflag;
        if ((p_flag[0] && dimflag[0]) || (p_flag[1] && dimflag[1]) ||
            (p_flag[2] && dimflag[2]) || (p_flag[3] && dimflag[3]) ||
            (p_flag[4] && dimflag[4]) || (p_flag[5] && dimflag[5]))
          error->all(FLERR,"Cannot use fix npt and fix deform on "
                     "same component of stress tensor");
      }

  // set temperature and pressure ptrs

  int icompute = modify->find_compute(id_temp);
  if (icompute < 0)
    error->all(FLERR,"Temperature ID for fix nvt/npt does not exist");
  temperature = modify->compute[icompute];

  if (temperature->tempbias) which = BIAS;
  else which = NOBIAS;

  if (pstat_flag) {
    icompute = modify->find_compute(id_press);
    if (icompute < 0)
      error->all(FLERR,"Pressure ID for fix npt/nph does not exist");
    pressure = modify->compute[icompute];
  }

  // set timesteps and frequencies

  dtv = update->dt;
  dtf = 0.5 * update->dt * force->ftm2v;
  dthalf = 0.5 * update->dt;
  dt4 = 0.25 * update->dt;
  dt8 = 0.125 * update->dt;
  dto = dthalf;

  p_freq_max = 0.0;
  if (pstat_flag) {
    p_freq_max = MAX(p_freq[0],p_freq[1]);
    p_freq_max = MAX(p_freq_max,p_freq[2]);
    if (pstyle == TRICLINIC) {
      p_freq_max = MAX(p_freq_max,p_freq[3]);
      p_freq_max = MAX(p_freq_max,p_freq[4]);
      p_freq_max = MAX(p_freq_max,p_freq[5]);
    }
    pdrag_factor = 1.0 - (update->dt * p_freq_max * drag / nc_pchain);

    *interval_pdrag_factor = 1.0 - (update->dt * p_freq_max * (*interval_drag) / nc_pchain);  // par pdrag_factor
  }

  if (tstat_flag) {
    tdrag_factor = 1.0 - (update->dt * t_freq * drag / nc_tchain);

    *interval_tdrag_factor = 1.0 - (update->dt * t_freq * (*interval_drag) / nc_tchain);      // par tdrag_factor
  }
  // tally the number of dimensions that are barostatted
  // set initial volume and reference cell, if not already done

  if (pstat_flag) {
    pdim = p_flag[0] + p_flag[1] + p_flag[2];
    if (vol0 == 0.0) {
      if (dimension == 3) vol0 = domain->xprd * domain->yprd * domain->zprd;
      else vol0 = domain->xprd * domain->yprd;
      h0_inv[0] = domain->h_inv[0];
      h0_inv[1] = domain->h_inv[1];
      h0_inv[2] = domain->h_inv[2];
      h0_inv[3] = domain->h_inv[3];
      h0_inv[4] = domain->h_inv[4];
      h0_inv[5] = domain->h_inv[5];

    }
  }

  boltz = force->boltz;
  nktv2p = force->nktv2p;

  if (force->kspace) kspace_flag = 1;
  else kspace_flag = 0;

  if (strstr(update->integrate_style,"respa")) {
    nlevels_respa = ((Respa *) update->integrate)->nlevels;
    step_respa = ((Respa *) update->integrate)->step;
    dto = 0.5*step_respa[0];
  }

  // detect if any rigid fixes exist so rigid bodies move when box is remapped
  // rfix[] = indices to each fix rigid

  delete [] rfix;
  nrigid = 0;
  rfix = NULL;

  for (int i = 0; i < modify->nfix; i++)
    if (modify->fix[i]->rigid_flag) nrigid++;
  if (nrigid) {
    rfix = new int[nrigid];
    nrigid = 0;
    for (int i = 0; i < modify->nfix; i++)
      if (modify->fix[i]->rigid_flag) rfix[nrigid++] = i;
  }
}

/* ----------------------------------------------------------------------
   compute T,P before integrator starts
------------------------------------------------------------------------- */

void FixNH::setup(int vflag)
{
  // initialize some quantities that were not available earlier

  tdof = temperature->dof;

  // t_target is needed by NPH and NPT in compute_scalar()
  // If no thermostat or using fix nphug,
  // t_target must be defined by other means.

  if (tstat_flag && strcmp(style,"nphug") != 0) {
    compute_temp_target();
  } else if (pstat_flag) {

    // t0 = reference temperature for masses
    // cannot be done in init() b/c temperature cannot be called there
    // is b/c Modify::init() inits computes after fixes due to dof dependence
    // guesstimate a unit-dependent t0 if actual T = 0.0
    // if it was read in from a restart file, leave it be

    if (t0 == 0.0) {
      t0 = temperature->compute_scalar();
      if (t0 == 0.0) {
        if (strcmp(update->unit_style,"lj") == 0) t0 = 1.0;
        else t0 = 300.0;
      }
    }
    t_target = t0;
    *interval_t_target = interval(t_target);
  }

  if (pstat_flag) compute_press_target();

  t_current = temperature->compute_scalar();
  // par
  double t_current_ub = temperature->compute_scalar_ub();
  double t_current_lb = temperature->compute_scalar_lb();
  *interval_t_current = interval(t_current_lb,t_current_ub);

  // std::cout << "pstat_flag = " << pstat_flag << std::endl; // pstat_flag = 1
  // std::cout << "pstyle = " << pstyle << std::endl; // pstyle = 0

  if (pstat_flag) {
    if (pstyle == ISO) {
      pressure->compute_scalar();
      // par
      pressure->compute_scalar_ub();
      pressure->compute_scalar_lb();
      // begin debug
      // status: not used
      // std::cout << "compute_scalar_lb is running ... " << std::endl;
      // end debug
    }
    else {
      pressure->compute_vector();

      // par: do not have functions compute_vector _ub() and _lb() -- see compute_pressure.cpp
      // pressure->compute_vector_ub();
      // pressure->compute_vector_lb();
      // std::cout << "compute_vector() is running " << std::endl;
    }
    couple();
    pressure->addstep(update->ntimestep+1);
  }

  // masses and initial forces on thermostat variables

  if (tstat_flag) {
    eta_mass[0] = tdof * boltz * t_target / (t_freq*t_freq);
    ivector_eta_mass[0] = interval(tdof * boltz * t_target / ((*interval_t_freq)*(*interval_t_freq)));

    // begin debug
    // status - almost the same, accuracy problem
    // std::cout << "t_freq = "            << t_freq << std::endl;
    // std::cout << "*interval_t_freq = "  << *interval_t_freq << std::endl;
    // end debug

    for (int ich = 1; ich < mtchain; ich++) {
      eta_mass[ich] = boltz * t_target / (t_freq*t_freq);
      ivector_eta_mass[ich] = interval(boltz * t_target / ((*interval_t_freq)*(*interval_t_freq)));

      }
    for (int ich = 1; ich < mtchain; ich++) {
      eta_dotdot[ich] = (eta_mass[ich-1]*eta_dot[ich-1]*eta_dot[ich-1] -
                         boltz * t_target) / eta_mass[ich];
      ivector_eta_dotdot[ich] = interval((interval(ivector_eta_mass[ich-1])*interval(ivector_eta_dot[ich-1])*interval(ivector_eta_dot[ich-1]) -
                               boltz * t_target) / interval(ivector_eta_mass[ich]) );
    }
  }

  // masses and initial forces on barostat variables

  if (pstat_flag) {
    double kt = boltz * t_target;
    double nkt = atom->natoms * kt;

    for (int i = 0; i < 3; i++)
      if (p_flag[i]) {
        omega_mass[i] = nkt/(p_freq[i]*p_freq[i]);
        ivector_omega_mass[i] = interval(omega_mass[i]); // par omega_mass
      }
    if (pstyle == TRICLINIC) {
      for (int i = 3; i < 6; i++) {
        if (p_flag[i]) {
          omega_mass[i] = nkt/(p_freq[i]*p_freq[i]);
          ivector_omega_mass[i] = interval(omega_mass[i]); // par omega_mass
        }
      }
    }

  // masses and initial forces on barostat thermostat variables

    if (mpchain) {
      etap_mass[0] = boltz * t_target / (p_freq_max*p_freq_max);
      ivector_etap_mass[0] = interval(etap_mass[0]); 
      for (int ich = 1; ich < mpchain; ich++) {
        etap_mass[ich] = boltz * t_target / (p_freq_max*p_freq_max);
        ivector_etap_mass[ich] = interval(etap_mass[ich]);
      }      
      for (int ich = 1; ich < mpchain; ich++) {
        etap_dotdot[ich] = (etap_mass[ich-1]*etap_dot[ich-1]*etap_dot[ich-1] - boltz * t_target) / etap_mass[ich];
        ivector_etap_dotdot[ich] = ( interval(ivector_etap_mass[ich-1])*interval(ivector_etap_dot[ich-1])*interval(ivector_etap_dot[ich-1]) - boltz * t_target) / interval(ivector_etap_mass[ich]);
      }
    }
  }
}

/* ----------------------------------------------------------------------
   1st half of Verlet update
------------------------------------------------------------------------- */

void FixNH::initial_integrate(int vflag)
{
  // update eta_press_dot

  if (pstat_flag && mpchain) nhc_press_integrate();

  // update eta_dot

  if (tstat_flag) {
    compute_temp_target();
    nhc_temp_integrate();
  }

  // need to recompute pressure to account for change in KE
  // t_current is up-to-date, but compute_temperature is not
  // compute appropriately coupled elements of mvv_current

  if (pstat_flag) {
    if (pstyle == ISO) {
      temperature->compute_scalar();
      pressure->compute_scalar();
    } else {
      temperature->compute_vector();
      pressure->compute_vector();
    }
    couple();
    pressure->addstep(update->ntimestep+1);
  }

  if (pstat_flag) {
    compute_press_target();
    nh_omega_dot();
    nh_v_press();
  }

  nve_v();

  // remap simulation box by 1/2 step

  if (pstat_flag) remap();

  nve_x();

  // remap simulation box by 1/2 step
  // redo KSpace coeffs since volume has changed

  if (pstat_flag) {
    remap();
    if (kspace_flag) force->kspace->setup();
  }
}

/* ----------------------------------------------------------------------
   2nd half of Verlet update
------------------------------------------------------------------------- */

void FixNH::final_integrate()
{
  nve_v();

  if (pstat_flag) nh_v_press();

  // compute new T,P
  // compute appropriately coupled elements of mvv_current

  t_current = temperature->compute_scalar();
  if (pstat_flag) {
    if (pstyle == ISO) pressure->compute_scalar();
    else pressure->compute_vector();
    couple();
    pressure->addstep(update->ntimestep+1);
  }

  if (pstat_flag) nh_omega_dot();

  // update eta_dot
  // update eta_press_dot

  if (tstat_flag) nhc_temp_integrate();
  if (pstat_flag && mpchain) nhc_press_integrate();
}

/* ---------------------------------------------------------------------- */

void FixNH::initial_integrate_respa(int vflag, int ilevel, int iloop)
{
  // set timesteps by level

  dtv = step_respa[ilevel];
  dtf = 0.5 * step_respa[ilevel] * force->ftm2v;
  dthalf = 0.5 * step_respa[ilevel];

  // outermost level - update eta_dot and omega_dot, apply to v
  // all other levels - NVE update of v
  // x,v updates only performed for atoms in group

  if (ilevel == nlevels_respa-1) {

    // update eta_press_dot

    if (pstat_flag && mpchain) nhc_press_integrate();

    // update eta_dot

    if (tstat_flag) {
      compute_temp_target();
      nhc_temp_integrate();
    }

    // recompute pressure to account for change in KE
    // t_current is up-to-date, but compute_temperature is not
    // compute appropriately coupled elements of mvv_current

    if (pstat_flag) {
      if (pstyle == ISO) {
        temperature->compute_scalar();
        pressure->compute_scalar();
      } else {
               temperature->compute_vector();
        pressure->compute_vector();
      }
      couple();
      pressure->addstep(update->ntimestep+1);
    }

    if (pstat_flag) {
      compute_press_target();
      nh_omega_dot();
      nh_v_press();
    }

    nve_v();

  } else nve_v();

  // innermost level - also update x only for atoms in group
  // if barostat, perform 1/2 step remap before and after

  if (ilevel == 0) {
    if (pstat_flag) remap();
    nve_x();
    if (pstat_flag) remap();
  }

  // if barostat, redo KSpace coeffs at outermost level,
  // since volume has changed

  if (ilevel == nlevels_respa-1 && kspace_flag && pstat_flag)
    force->kspace->setup();
}

/* ---------------------------------------------------------------------- */

void FixNH::final_integrate_respa(int ilevel, int iloop)
{
  // set timesteps by level

  dtf = 0.5 * step_respa[ilevel] * force->ftm2v;
  dthalf = 0.5 * step_respa[ilevel];

  // outermost level - update eta_dot and omega_dot, apply via final_integrate
  // all other levels - NVE update of v

  if (ilevel == nlevels_respa-1) final_integrate();
  else nve_v();
}

/* ---------------------------------------------------------------------- */

void FixNH::couple()
{
  double *tensor = pressure->vector;
  // par
  double *tensor_ub = pressure->vector_ub;
  double *tensor_lb = pressure->vector_lb;
  for (int i = 0; i < 6; i++) {
    ivector_tensor[i] = interval(tensor_lb[i],tensor_ub[i]);
    // begin debug
    // status: OK
    // std::cout << "tensor_ub[" << i << "] = " << tensor_ub[i] << std::endl;
    // std::cout << "tensor_lb[" << i << "] = " << tensor_lb[i] << std::endl;
    // end debug
  }

  // test
  pressure->compute_scalar_lb(); // compute pressure->scalar_lb
  pressure->compute_scalar_ub(); // compute pressure->scalar_ub

  if (pstyle == ISO) {
      p_current[0] = p_current[1] = p_current[2] = pressure->scalar;
      ivector_p_current[0] = ivector_p_current[1] = ivector_p_current[2] = interval(pressure->scalar_lb,pressure->scalar_ub);

      // begin debug
      // status: bug NaN - red flag
      // std::cout << "pressure->scalar_lb = " << pressure->scalar_lb << std::endl;
      // std::cout << "pressure->scalar_ub = " << pressure->scalar_ub << std::endl;
      // end debug
    }
  else if (pcouple == XYZ) {
    double ave = 1.0/3.0 * (tensor[0] + tensor[1] + tensor[2]);
    p_current[0] = p_current[1] = p_current[2] = ave;

    interval interval_ave = 1.0/3.0 * ( interval(ivector_tensor[0]) + interval(ivector_tensor[1]) + interval(ivector_tensor[2]) );
    ivector_p_current[0] = ivector_p_current[1] = ivector_p_current[2] = interval_ave;
  } else if (pcouple == XY) {
    double ave = 0.5 * (tensor[0] + tensor[1]);
    p_current[0] = p_current[1] = ave;
    p_current[2] = tensor[2];

    interval interval_ave = 0.5 * ( interval(ivector_tensor[0]) + interval(ivector_tensor[1]) );
    ivector_p_current[0] = ivector_p_current[1] = interval_ave;
    ivector_p_current[2] = ivector_tensor[2];
  } else if (pcouple == YZ) {
    double ave = 0.5 * (tensor[1] + tensor[2]);
    p_current[1] = p_current[2] = ave;
    p_current[0] = tensor[0];

    interval interval_ave = 0.5 * ( interval(ivector_tensor[1]) + interval(ivector_tensor[2]) );
    ivector_p_current[1] = ivector_p_current[2] = interval_ave;
    ivector_p_current[0] = ivector_tensor[0];
  } else if (pcouple == XZ) {
    double ave = 0.5 * (tensor[0] + tensor[2]);
    p_current[0] = p_current[2] = ave;
    p_current[1] = tensor[1];

    interval interval_ave = 0.5 * ( interval(ivector_tensor[0]) + interval(ivector_tensor[2]) );
    ivector_p_current[0] = ivector_p_current[2] = interval_ave;
    ivector_p_current[1] = ivector_tensor[1];
  } else {
    p_current[0] = tensor[0];
    p_current[1] = tensor[1];
    p_current[2] = tensor[2];

    ivector_p_current[0] = ivector_tensor[0];
    ivector_p_current[1] = ivector_tensor[1];
    ivector_p_current[2] = ivector_tensor[2];
  }

  // switch order from xy-xz-yz to Voigt

  if (pstyle == TRICLINIC) {
    p_current[3] = tensor[5];
    p_current[4] = tensor[4];
    p_current[5] = tensor[3];

    ivector_p_current[3] = ivector_tensor[5];
    ivector_p_current[4] = ivector_tensor[4];
    ivector_p_current[5] = ivector_tensor[3];
  }

  // begin debug
  // status: bug NaN
  // for (int j = 0; j < 6; j++) {
  //   std::cout << "p_current[" << j << "] = " << p_current[j] << std::endl;
  //   std::cout << "ivector_p_current[" << j << "] = " << ivector_p_current[j] << std::endl;
  // }
  // end debug
}

/* ----------------------------------------------------------------------
   change box size
   remap all atoms or dilate group atoms depending on allremap flag
   if rigid bodies exist, scale rigid body centers-of-mass
------------------------------------------------------------------------- */

void FixNH::remap()
{
  int i;
  double oldlo,oldhi;
  double expfac;

  double **x = atom->x;
  double **xlb = atom->xlb;
  double **xub = atom->xub;

  int *mask = atom->mask;
  int nlocal = atom->nlocal;
  double *h = domain->h;

  // begin debug 
  // status: yes - running in this example
  // std::cout << "remap() is running ... " << std::endl;
  // end debug

  // omega is not used, except for book-keeping

  for (int i = 0; i < 6; i++) omega[i] += dto*omega_dot[i];

  // convert pertinent atoms and rigid bodies to lamda coords

  // begin debug
  // status: allremap = 1
  // std::cout << "allremap = " << allremap << std::endl;
  // end debug

  if (allremap) domain->x2lamda(nlocal);
  else {
    for (i = 0; i < nlocal; i++)
      if (mask[i] & dilate_group_bit) { 
        domain->x2lamda(x[i],x[i]);

        // begin debug
        // status: no -- not running 
        // std::cout << "running here ... " << std::endl;
        // end debug
      }
  }

  if (nrigid)
    for (i = 0; i < nrigid; i++)
      modify->fix[rfix[i]]->deform(0);

  // reset global and local box to new size/shape

  // this operation corresponds to applying the
  // translate and scale operations
  // corresponding to the solution of the following ODE:
  //
  // h_dot = omega_dot * h
  //
  // where h_dot, omega_dot and h are all upper-triangular
  // 3x3 tensors. In Voigt notation, the elements of the
  // RHS product tensor are:
  // h_dot = [0*0, 1*1, 2*2, 1*3+3*2, 0*4+5*3+4*2, 0*5+5*1]
  //
  // Ordering of operations preserves time symmetry.

  double dto2 = dto/2.0;
  double dto4 = dto/4.0;
  double dto8 = dto/8.0;

  // off-diagonal components, first half

  if (pstyle == TRICLINIC) {

    if (p_flag[4]) {
      expfac = exp(dto8*omega_dot[0]);
      h[4] *= expfac;
      h[4] += dto4*(omega_dot[5]*h[3]+omega_dot[4]*h[2]);
      h[4] *= expfac;

      *interval_expfac = exp(dto8*interval(ivector_omega_dot[0]));
      ivector_h[4] *= (*interval_expfac);
      ivector_h[4] = interval(ivector_h[4]) + dto4*(interval(ivector_omega_dot[5])*interval(ivector_h[3])+interval(ivector_omega_dot[4])*interval(ivector_h[2]));
      ivector_h[4] *= (*interval_expfac);
    }

    if (p_flag[3]) {
      expfac = exp(dto4*omega_dot[1]);
      h[3] *= expfac;
      h[3] += dto2*(omega_dot[3]*h[2]);
      h[3] *= expfac;

      *interval_expfac = exp(dto4*interval(ivector_omega_dot[1]));
      ivector_h[3] *= (*interval_expfac);
      ivector_h[3] = interval(ivector_h[3]) + dto2*(interval(ivector_omega_dot[3])*interval(ivector_h[2]));
      ivector_h[3] *= (*interval_expfac);
    }

    if (p_flag[5]) {
      expfac = exp(dto4*omega_dot[0]);
      h[5] *= expfac;
      h[5] += dto2*(omega_dot[5]*h[1]);
      h[5] *= expfac;

      *interval_expfac = exp(dto4*interval(ivector_omega_dot[0]));
      ivector_h[5] *= (*interval_expfac);
      ivector_h[5] = interval(ivector_h[5]) + dto2*(interval(ivector_omega_dot[5])*interval(ivector_h[1]));
      ivector_h[5] *= (*interval_expfac);
    }

    if (p_flag[4]) {
      expfac = exp(dto8*omega_dot[0]);
      h[4] *= expfac;
      h[4] += dto4*(omega_dot[5]*h[3]+omega_dot[4]*h[2]);
      h[4] *= expfac;

      *interval_expfac = exp(dto8*interval(ivector_omega_dot[0]));
      ivector_h[4] *= (*interval_expfac);
      ivector_h[4] = interval(ivector_h[4]) + dto4*(interval(ivector_omega_dot[5])*interval(ivector_h[3])+interval(ivector_omega_dot[4])*interval(ivector_h[2]));
      ivector_h[4] *= (*interval_expfac);
    }
  }

  // scale diagonal components
  // scale tilt factors with cell, if set

  if (p_flag[0]) {
    oldlo = domain->boxlo[0];
    oldhi = domain->boxhi[0];
    expfac = exp(dto*omega_dot[0]);
    *interval_expfac = exp(dto*interval(ivector_omega_dot[0]));

    domain->boxlo[0] = (oldlo-fixedpoint[0])*expfac + fixedpoint[0];
    domain->boxhi[0] = (oldhi-fixedpoint[0])*expfac + fixedpoint[0];
  }

  if (p_flag[1]) {
    oldlo = domain->boxlo[1];
    oldhi = domain->boxhi[1];
    expfac = exp(dto*omega_dot[1]);
    *interval_expfac = exp(dto*interval(ivector_omega_dot[1]));

    domain->boxlo[1] = (oldlo-fixedpoint[1])*expfac + fixedpoint[1];
    domain->boxhi[1] = (oldhi-fixedpoint[1])*expfac + fixedpoint[1];
    if (scalexy) {
      h[5] *= expfac;
      ivector_h[5] *= (*interval_expfac);
    }
  }

  if (p_flag[2]) {
    oldlo = domain->boxlo[2];
    oldhi = domain->boxhi[2];
    expfac = exp(dto*omega_dot[2]);
    *interval_expfac = exp(dto*interval(ivector_omega_dot[2]));

    domain->boxlo[2] = (oldlo-fixedpoint[2])*expfac + fixedpoint[2];
    domain->boxhi[2] = (oldhi-fixedpoint[2])*expfac + fixedpoint[2];
    if (scalexz) {
      h[4] *= expfac;
      ivector_h[4] *= (*interval_expfac);
    }
    if (scaleyz) {
      h[3] *= expfac;
      ivector_h[3] *= (*interval_expfac);
    }
  }

  // off-diagonal components, second half

  if (pstyle == TRICLINIC) {

    if (p_flag[4]) {
      expfac = exp(dto8*omega_dot[0]);
      h[4] *= expfac;
      h[4] += dto4*(omega_dot[5]*h[3]+omega_dot[4]*h[2]);
      h[4] *= expfac;

      *interval_expfac = exp(dto8*interval(ivector_omega_dot[0]));
      ivector_h[4] *= (*interval_expfac);
      ivector_h[4] = interval(ivector_h[4]) + dto4*(interval(ivector_omega_dot[5])*interval(ivector_h[3])+interval(ivector_omega_dot[4])*interval(ivector_h[2]));
      ivector_h[4] *= (*interval_expfac);
    }

    if (p_flag[3]) {
      expfac = exp(dto4*omega_dot[1]);
      h[3] *= expfac;
      h[3] += dto2*(omega_dot[3]*h[2]);
      h[3] *= expfac;

      *interval_expfac = exp(dto4*interval(ivector_omega_dot[1]));
      ivector_h[3] *= (*interval_expfac);
      ivector_h[3] = interval(ivector_h[3]) + dto2*(interval(ivector_omega_dot[3])*interval(ivector_h[2]));
      ivector_h[3] *= (*interval_expfac);
    }

    if (p_flag[5]) {
      expfac = exp(dto4*omega_dot[0]);
      h[5] *= expfac;
      h[5] += dto2*(omega_dot[5]*h[1]);
      h[5] *= expfac;

      *interval_expfac = exp(dto4*interval(ivector_omega_dot[0]));
      ivector_h[5] *= (*interval_expfac);
      ivector_h[5] = interval(ivector_h[5]) + dto2*(interval(ivector_omega_dot[5])*interval(ivector_h[1]));
      ivector_h[5] *= (*interval_expfac);
    }

    if (p_flag[4]) {
      expfac = exp(dto8*omega_dot[0]);
      h[4] *= expfac;
      h[4] += dto4*(omega_dot[5]*h[3]+omega_dot[4]*h[2]);
      h[4] *= expfac;

      *interval_expfac = exp(dto8*interval(ivector_omega_dot[0]));
      ivector_h[4] *= (*interval_expfac);
      ivector_h[4] = interval(ivector_h[4]) + dto4*(interval(ivector_omega_dot[5])*interval(ivector_h[3])+interval(ivector_omega_dot[4])*interval(ivector_h[2]));
      ivector_h[4] *= (*interval_expfac);
    }

  }

  domain->yz = h[3];
  domain->xz = h[4];
  domain->xy = h[5];

  // tilt factor to cell length ratio can not exceed TILTMAX in one step

  if (domain->yz < -TILTMAX*domain->yprd ||
      domain->yz > TILTMAX*domain->yprd ||
      domain->xz < -TILTMAX*domain->xprd ||
      domain->xz > TILTMAX*domain->xprd ||
      domain->xy < -TILTMAX*domain->xprd ||
      domain->xy > TILTMAX*domain->xprd)
    error->all(FLERR,"Fix npt/nph has tilted box too far in one step - "
               "periodic cell is too far from equilibrium state");

  domain->set_global_box();
  domain->set_local_box();

  // convert pertinent atoms and rigid bodies back to box coords

  if (allremap) domain->lamda2x(nlocal);
  else {
    for (i = 0; i < nlocal; i++)
      if (mask[i] & dilate_group_bit)
        domain->lamda2x(x[i],x[i]);
  }

  if (nrigid)
    for (i = 0; i < nrigid; i++)
      modify->fix[rfix[i]]->deform(1);
}

/* ----------------------------------------------------------------------
   pack entire state of Fix into one write
------------------------------------------------------------------------- */

void FixNH::write_restart(FILE *fp)
{
  int nsize = size_restart_global();

  double *list;
  memory->create(list,nsize,"nh:list");

  pack_restart_data(list);

  if (comm->me == 0) {
    int size = nsize * sizeof(double);
    fwrite(&size,sizeof(int),1,fp);
    fwrite(list,sizeof(double),nsize,fp);
  }

  memory->destroy(list);
}

/* ----------------------------------------------------------------------
    calculate the number of data to be packed
------------------------------------------------------------------------- */

int FixNH::size_restart_global()
{
  int nsize = 2;
  if (tstat_flag) nsize += 1 + 2*mtchain;
  if (pstat_flag) {
    nsize += 16 + 2*mpchain;
    if (deviatoric_flag) nsize += 6;
  }

  return nsize;
}

/* ----------------------------------------------------------------------
   pack restart data
------------------------------------------------------------------------- */

int FixNH::pack_restart_data(double *list)
{
  int n = 0;

  list[n++] = tstat_flag;
  if (tstat_flag) {
    list[n++] = mtchain;
    for (int ich = 0; ich < mtchain; ich++)
      list[n++] = eta[ich];
    for (int ich = 0; ich < mtchain; ich++)
      list[n++] = eta_dot[ich];
  }

  list[n++] = pstat_flag;
  if (pstat_flag) {
    list[n++] = omega[0];
    list[n++] = omega[1];
    list[n++] = omega[2];
    list[n++] = omega[3];
    list[n++] = omega[4];
    list[n++] = omega[5];
    list[n++] = omega_dot[0];
    list[n++] = omega_dot[1];
    list[n++] = omega_dot[2];
    list[n++] = omega_dot[3];
    list[n++] = omega_dot[4];
    list[n++] = omega_dot[5];
    list[n++] = vol0;
    list[n++] = t0;
    list[n++] = mpchain;
    if (mpchain) {
      for (int ich = 0; ich < mpchain; ich++)
        list[n++] = etap[ich];
      for (int ich = 0; ich < mpchain; ich++)
        list[n++] = etap_dot[ich];
    }

    list[n++] = deviatoric_flag;
    if (deviatoric_flag) {
      list[n++] = h0_inv[0];
      list[n++] = h0_inv[1];
      list[n++] = h0_inv[2];
      list[n++] = h0_inv[3];
      list[n++] = h0_inv[4];
      list[n++] = h0_inv[5];
    }
  }

  return n;
}

/* ----------------------------------------------------------------------
   use state info from restart file to restart the Fix
------------------------------------------------------------------------- */

void FixNH::restart(char *buf)
{
  int n = 0;
  double *list = (double *) buf;
  int flag = static_cast<int> (list[n++]);
  if (flag) {
    int m = static_cast<int> (list[n++]);
    if (tstat_flag && m == mtchain) {
      for (int ich = 0; ich < mtchain; ich++)
        eta[ich] = list[n++];
      for (int ich = 0; ich < mtchain; ich++)
        eta_dot[ich] = list[n++];
    } else n += 2*m;
  }
  flag = static_cast<int> (list[n++]);
  if (flag) {
    omega[0] = list[n++];
    omega[1] = list[n++];
    omega[2] = list[n++];
    omega[3] = list[n++];
    omega[4] = list[n++];
    omega[5] = list[n++];
    omega_dot[0] = list[n++];
    omega_dot[1] = list[n++];
    omega_dot[2] = list[n++];
    omega_dot[3] = list[n++];
    omega_dot[4] = list[n++];
    omega_dot[5] = list[n++];
    vol0 = list[n++];
    t0 = list[n++];
    int m = static_cast<int> (list[n++]);
    if (pstat_flag && m == mpchain) {
      for (int ich = 0; ich < mpchain; ich++)
        etap[ich] = list[n++];
      for (int ich = 0; ich < mpchain; ich++)
        etap_dot[ich] = list[n++];
    } else n+=2*m;
    flag = static_cast<int> (list[n++]);
    if (flag) {
      h0_inv[0] = list[n++];
      h0_inv[1] = list[n++];
      h0_inv[2] = list[n++];
      h0_inv[3] = list[n++];
      h0_inv[4] = list[n++];
      h0_inv[5] = list[n++];
    }
  }
}

/* ---------------------------------------------------------------------- */

int FixNH::modify_param(int narg, char **arg)
{
  if (strcmp(arg[0],"temp") == 0) {
    if (narg < 2) error->all(FLERR,"Illegal fix_modify command");
    if (tflag) {
      modify->delete_compute(id_temp);
      tflag = 0;
    }
    delete [] id_temp;
    int n = strlen(arg[1]) + 1;
    id_temp = new char[n];
    strcpy(id_temp,arg[1]);

    int icompute = modify->find_compute(arg[1]);
    if (icompute < 0)
      error->all(FLERR,"Could not find fix_modify temperature ID");
    temperature = modify->compute[icompute];

    if (temperature->tempflag == 0)
      error->all(FLERR,
                 "Fix_modify temperature ID does not compute temperature");
    if (temperature->igroup != 0 && comm->me == 0)
      error->warning(FLERR,"Temperature for fix modify is not for group all");

    // reset id_temp of pressure to new temperature ID

    if (pstat_flag) {
      icompute = modify->find_compute(id_press);
      if (icompute < 0)
        error->all(FLERR,"Pressure ID for fix modify does not exist");
      modify->compute[icompute]->reset_extra_compute_fix(id_temp);
    }

    return 2;

  } else if (strcmp(arg[0],"press") == 0) {
    if (narg < 2) error->all(FLERR,"Illegal fix_modify command");
    if (!pstat_flag) error->all(FLERR,"Illegal fix_modify command");
    if (pflag) {
      modify->delete_compute(id_press);
      pflag = 0;
    }
    delete [] id_press;
    int n = strlen(arg[1]) + 1;
    id_press = new char[n];
    strcpy(id_press,arg[1]);

    int icompute = modify->find_compute(arg[1]);
    if (icompute < 0) error->all(FLERR,"Could not find fix_modify pressure ID");
    pressure = modify->compute[icompute];

    if (pressure->pressflag == 0)
      error->all(FLERR,"Fix_modify pressure ID does not compute pressure");
    return 2;
  }

  return 0;
}

/* ---------------------------------------------------------------------- */

double FixNH::compute_scalar()
{
  int i;
  double volume;
  double energy;
  double kt = boltz * t_target;
  double lkt_press = kt;
  int ich;
  if (dimension == 3) volume = domain->xprd * domain->yprd * domain->zprd;
  else volume = domain->xprd * domain->yprd;

  energy = 0.0;

  // thermostat chain energy is equivalent to Eq. (2) in
  // Martyna, Tuckerman, Tobias, Klein, Mol Phys, 87, 1117
  // Sum(0.5*p_eta_k^2/Q_k,k=1,M) + L*k*T*eta_1 + Sum(k*T*eta_k,k=2,M),
  // where L = tdof
  //       M = mtchain
  //       p_eta_k = Q_k*eta_dot[k-1]
  //       Q_1 = L*k*T/t_freq^2
  //       Q_k = k*T/t_freq^2, k > 1

  if (tstat_flag) {
    energy += ke_target * eta[0] + 0.5*eta_mass[0]*eta_dot[0]*eta_dot[0];
    for (ich = 1; ich < mtchain; ich++)
      energy += kt * eta[ich] + 0.5*eta_mass[ich]*eta_dot[ich]*eta_dot[ich];
  }

  // barostat energy is equivalent to Eq. (8) in
  // Martyna, Tuckerman, Tobias, Klein, Mol Phys, 87, 1117
  // Sum(0.5*p_omega^2/W + P*V),
  // where N = natoms
  //       p_omega = W*omega_dot
  //       W = N*k*T/p_freq^2
  //       sum is over barostatted dimensions

  if (pstat_flag) {
    for (i = 0; i < 3; i++)
      if (p_flag[i])
        energy += 0.5*omega_dot[i]*omega_dot[i]*omega_mass[i] +
          p_hydro*(volume-vol0) / (pdim*nktv2p);

    if (pstyle == TRICLINIC) {
      for (i = 3; i < 6; i++)
        if (p_flag[i])
          energy += 0.5*omega_dot[i]*omega_dot[i]*omega_mass[i];
    }

    // extra contributions from thermostat chain for barostat

    if (mpchain) {
      energy += lkt_press * etap[0] + 0.5*etap_mass[0]*etap_dot[0]*etap_dot[0];
      for (ich = 1; ich < mpchain; ich++)
        energy += kt * etap[ich] +
          0.5*etap_mass[ich]*etap_dot[ich]*etap_dot[ich];
    }

    // extra contribution from strain energy

    if (deviatoric_flag) energy += compute_strain_energy();
  }

  return energy;
}

/* ----------------------------------------------------------------------
   return a single element of the following vectors, in this order:
      eta[tchain], eta_dot[tchain], omega[ndof], omega_dot[ndof]
      etap[pchain], etap_dot[pchain], PE_eta[tchain], KE_eta_dot[tchain]
      PE_omega[ndof], KE_omega_dot[ndof], PE_etap[pchain], KE_etap_dot[pchain]
      PE_strain[1]
  if no thermostat exists, related quantities are omitted from the list
  if no barostat exists, related quantities are omitted from the list
  ndof = 1,3,6 degrees of freedom for pstyle = ISO,ANISO,TRI
------------------------------------------------------------------------- */

double FixNH::compute_vector(int n)
{
  int ilen;

  if (tstat_flag) {
    ilen = mtchain;
    if (n < ilen) return eta[n];
    n -= ilen;
    ilen = mtchain;
    if (n < ilen) return eta_dot[n];
    n -= ilen;
  }

  if (pstat_flag) {
    if (pstyle == ISO) {
      ilen = 1;
      if (n < ilen) return omega[n];
      n -= ilen;
    } else if (pstyle == ANISO) {
      ilen = 3;
      if (n < ilen) return omega[n];
      n -= ilen;
    } else {
      ilen = 6;
      if (n < ilen) return omega[n];
      n -= ilen;
    }

    if (pstyle == ISO) {
      ilen = 1;
      if (n < ilen) return omega_dot[n];
      n -= ilen;
    } else if (pstyle == ANISO) {
      ilen = 3;
      if (n < ilen) return omega_dot[n];
      n -= ilen;
    } else {
      ilen = 6;
      if (n < ilen) return omega_dot[n];
      n -= ilen;
    }

    if (mpchain) {
      ilen = mpchain;
      if (n < ilen) return etap[n];
      n -= ilen;
      ilen = mpchain;
      if (n < ilen) return etap_dot[n];
      n -= ilen;
    }
  }

  double volume;
  double kt = boltz * t_target;
  double lkt_press = kt;
  int ich;
  if (dimension == 3) volume = domain->xprd * domain->yprd * domain->zprd;
  else volume = domain->xprd * domain->yprd;

  if (tstat_flag) {
    ilen = mtchain;
    if (n < ilen) {
      ich = n;
      if (ich == 0)
        return ke_target * eta[0];
      else
        return kt * eta[ich];
    }
    n -= ilen;
    ilen = mtchain;
    if (n < ilen) {
      ich = n;
      if (ich == 0)
        return 0.5*eta_mass[0]*eta_dot[0]*eta_dot[0];
      else
        return 0.5*eta_mass[ich]*eta_dot[ich]*eta_dot[ich];
    }
    n -= ilen;
  }

  if (pstat_flag) {
    if (pstyle == ISO) {
      ilen = 1;
      if (n < ilen)
        return p_hydro*(volume-vol0) / nktv2p;
      n -= ilen;
    } else if (pstyle == ANISO) {
      ilen = 3;
      if (n < ilen) {
        if (p_flag[n])
          return p_hydro*(volume-vol0) / (pdim*nktv2p);
        else
          return 0.0;
      }
      n -= ilen;
    } else {
      ilen = 6;
      if (n < ilen) {
        if (n > 2) return 0.0;
        else if (p_flag[n])
          return p_hydro*(volume-vol0) / (pdim*nktv2p);
        else
          return 0.0;
      }
      n -= ilen;
    }

    if (pstyle == ISO) {
      ilen = 1;
      if (n < ilen)
        return pdim*0.5*omega_dot[n]*omega_dot[n]*omega_mass[n];
      n -= ilen;
    } else if (pstyle == ANISO) {
      ilen = 3;
      if (n < ilen) {
        if (p_flag[n])
          return 0.5*omega_dot[n]*omega_dot[n]*omega_mass[n];
        else return 0.0;
      }
      n -= ilen;
    } else {
      ilen = 6;
      if (n < ilen) {
        if (p_flag[n])
          return 0.5*omega_dot[n]*omega_dot[n]*omega_mass[n];
        else return 0.0;
      }
      n -= ilen;
    }

    if (mpchain) {
      ilen = mpchain;
      if (n < ilen) {
        ich = n;
        if (ich == 0) return lkt_press * etap[0];
        else return kt * etap[ich];
      }
      n -= ilen;
      ilen = mpchain;
      if (n < ilen) {
        ich = n;
        if (ich == 0)
          return 0.5*etap_mass[0]*etap_dot[0]*etap_dot[0];
        else
          return 0.5*etap_mass[ich]*etap_dot[ich]*etap_dot[ich];
      }
      n -= ilen;
    }

    if (deviatoric_flag) {
      ilen = 1;
      if (n < ilen)
        return compute_strain_energy();
      n -= ilen;
    }
  }

  return 0.0;
}

/* ---------------------------------------------------------------------- */

void FixNH::reset_target(double t_new)
{
  t_target = t_start = t_stop = t_new;
  *interval_t_target = *interval_t_start = *interval_t_stop = interval(t_new);
}

/* ---------------------------------------------------------------------- */

void FixNH::reset_dt()
{
  dtv = update->dt;
  dtf = 0.5 * update->dt * force->ftm2v;
  dthalf = 0.5 * update->dt;
  dt4 = 0.25 * update->dt;
  dt8 = 0.125 * update->dt;
  dto = dthalf;

  // If using respa, then remap is performed in innermost level

  if (strstr(update->integrate_style,"respa"))
    dto = 0.5*step_respa[0];

  if (pstat_flag) {
    pdrag_factor = 1.0 - (update->dt * p_freq_max * drag / nc_pchain);
    *interval_pdrag_factor = 1.0 - (update->dt * p_freq_max * (*interval_drag) / nc_pchain);
  }

  if (tstat_flag) {
    tdrag_factor = 1.0 - (update->dt * t_freq * drag / nc_tchain);
    *interval_tdrag_factor = 1.0 - (update->dt * t_freq * (*interval_drag) / nc_tchain);
  }
}

/* ----------------------------------------------------------------------
   extract thermostat properties
------------------------------------------------------------------------- */

void *FixNH::extract(const char *str, int &dim)
{
  dim=0;
  if (strcmp(str,"t_target") == 0) {
    return &t_target;
  } else if (strcmp(str,"mtchain") == 0) {
    return &mtchain;
  }
  dim=1;
  if (strcmp(str,"eta") == 0) {
    return &eta;
  }
  return NULL;
}

/* ----------------------------------------------------------------------
   perform half-step update of chain thermostat variables
------------------------------------------------------------------------- */

void FixNH::nhc_temp_integrate()
{
  int ich;
  double expfac;
  double kecurrent = tdof * boltz * t_current;

  *interval_kecurrent = tdof * boltz * (*interval_t_current);

  // Update masses, to preserve initial freq, if flag set

  if (eta_mass_flag) {
    eta_mass[0] = tdof * boltz * t_target / (t_freq*t_freq);
    ivector_eta_mass[0] = tdof * boltz * (*interval_t_target) / (t_freq*t_freq);

    for (int ich = 1; ich < mtchain; ich++) {
      eta_mass[ich] = boltz * t_target / (t_freq*t_freq);
      ivector_eta_mass[ich] = boltz * (*interval_t_target) / (t_freq*t_freq);
    }
  }

  if (eta_mass[0] > 0.0) {
    eta_dotdot[0] = (kecurrent - ke_target)/eta_mass[0];
    ivector_eta_dotdot[0] = ( (*interval_kecurrent) - (*interval_ke_target) ) / interval(ivector_eta_mass[0]);
  }
  else {
    eta_dotdot[0] = 0.0;
    ivector_eta_dotdot[0] = interval(0.0);
  }
  double ncfac = 1.0/nc_tchain;
  for (int iloop = 0; iloop < nc_tchain; iloop++) {

    for (ich = mtchain-1; ich > 0; ich--) {
      expfac = exp(-ncfac*dt8*eta_dot[ich+1]);
      eta_dot[ich] *= expfac;
      eta_dot[ich] += eta_dotdot[ich] * ncfac*dt4;
      eta_dot[ich] *= tdrag_factor;
      eta_dot[ich] *= expfac;

      *interval_expfac = exp(-ncfac*dt8*interval(ivector_eta_dot[ich+1]));
      ivector_eta_dot[ich] *= (*interval_expfac);
      ivector_eta_dot[ich] = interval(ivector_eta_dot[ich]) + interval(eta_dotdot[ich]) * ncfac*dt4;
      ivector_eta_dot[ich] *= (*interval_tdrag_factor);
      ivector_eta_dot[ich] *= (*interval_expfac);
    }

    expfac = exp(-ncfac*dt8*eta_dot[1]);
    eta_dot[0] *= expfac;
    eta_dot[0] += eta_dotdot[0] * ncfac*dt4;
    eta_dot[0] *= tdrag_factor;
    eta_dot[0] *= expfac;

    *interval_expfac = exp(-ncfac*dt8*interval(ivector_eta_dot[1]));
    ivector_eta_dot[0] *= (*interval_expfac);
    ivector_eta_dot[0] = interval(ivector_eta_dot[0]) + interval(ivector_eta_dotdot[0]) * ncfac*dt4;
    ivector_eta_dot[0] *= (*interval_tdrag_factor);
    ivector_eta_dot[0] *= (*interval_expfac);

    factor_eta = exp(-ncfac*dthalf*eta_dot[0]);
    *interval_factor_eta = exp(-ncfac*dthalf*interval(ivector_eta_dot[0]));

    // begin debug
    // if (update->ntimestep > (*sync_step)) {
      // std::cout << "update->ntimestep = " << update->ntimestep << std::endl;

      // status: bug at eta_dot and ivector_eta_dot[0] - totally diff
      // std::cout << "eta_dot[0] = " << eta_dot[0] << std::endl;
      // std::cout << "ivector_eta_dot[0] = " << interval(ivector_eta_dot[0]) << std::endl;

      // status: expfac and interval_expfac off some accuracy
      // std::cout << "expfac = " << expfac << std::endl;
      // std::cout << "*interval_expfac = " << *interval_expfac << std::endl;

      // status: bug at eta_dotdot[0] and ivector_eta_dotdot[0] totally diff
      // std::cout << "eta_dotdot[0] = " << eta_dotdot[0] << std::endl;
      // std::cout << "ivector_eta_dotdot[0] = " << interval(ivector_eta_dotdot[0]) << std::endl;

      // std::cout << std::endl;
    // }
    // end debug

    nh_v_temp(); // done implementing interval

    // rescale temperature due to velocity scaling
    // should not be necessary to explicitly recompute the temperature

    t_current *= factor_eta*factor_eta;
    kecurrent = tdof * boltz * t_current;

    *interval_t_current *= (*interval_factor_eta)*(*interval_factor_eta);
    *interval_kecurrent = tdof * boltz * (*interval_t_current);

    if (eta_mass[0] > 0.0) {
      eta_dotdot[0] = (kecurrent - ke_target)/eta_mass[0];
      ivector_eta_dotdot[0] = ((*interval_kecurrent) - (*interval_ke_target))/ interval(ivector_eta_mass[0]);
    }
    else {
      eta_dotdot[0] = 0.0;
      ivector_eta_dotdot[0] = interval(0.0);
    }

    for (ich = 0; ich < mtchain; ich++) {
      eta[ich] += ncfac*dthalf*eta_dot[ich];
      ivector_eta[ich] = interval(ivector_eta[ich]) + ncfac*dthalf*interval(ivector_eta_dot[ich]);
    }

    eta_dot[0] *= expfac;
    eta_dot[0] += eta_dotdot[0] * ncfac*dt4;
    eta_dot[0] *= expfac;

    ivector_eta_dot[0] *= (*interval_expfac);
    ivector_eta_dot[0] = interval(ivector_eta_dot[0]) + interval(ivector_eta_dotdot[0]) * ncfac*dt4;
    ivector_eta_dot[0] *= (*interval_expfac);

    for (ich = 1; ich < mtchain; ich++) {
      expfac = exp(-ncfac*dt8*eta_dot[ich+1]);
      eta_dot[ich] *= expfac;
      eta_dotdot[ich] = (eta_mass[ich-1]*eta_dot[ich-1]*eta_dot[ich-1] - boltz * t_target)/eta_mass[ich];
      eta_dot[ich] += eta_dotdot[ich] * ncfac*dt4;
      eta_dot[ich] *= expfac;

      *interval_expfac = exp(-ncfac*dt8*interval(ivector_eta_dot[ich+1]));
      ivector_eta_dot[ich] *= (*interval_expfac);
      ivector_eta_dotdot[ich] = (interval(ivector_eta_mass[ich-1])*interval(ivector_eta_dot[ich-1])*interval(ivector_eta_dot[ich-1]) - boltz * (*interval_t_target)) / interval(ivector_eta_mass[ich]);
      ivector_eta_dot[ich] = interval(ivector_eta_dot[ich]) + interval(ivector_eta_dotdot[ich]) * ncfac*dt4;
      ivector_eta_dot[ich] *= (*interval_expfac);
    }
  }
}

/* ----------------------------------------------------------------------
   perform half-step update of chain thermostat variables for barostat
   scale barostat velocities
------------------------------------------------------------------------- */

void FixNH::nhc_press_integrate()
{
  int ich,i;
  double expfac,factor_etap,kecurrent;
  double kt = boltz * t_target;
  double lkt_press = kt;

  // Update masses, to preserve initial freq, if flag set

  if (omega_mass_flag) {
    double nkt = atom->natoms * kt;
    for (int i = 0; i < 3; i++) {
      if (p_flag[i]) {
        omega_mass[i] = nkt/(p_freq[i]*p_freq[i]);
        ivector_omega_mass[i] = interval(omega_mass[i]); // par omega_mass
      }
    }
    if (pstyle == TRICLINIC) {
      for (int i = 3; i < 6; i++) {
        if (p_flag[i]) {
          omega_mass[i] = nkt/(p_freq[i]*p_freq[i]);
          ivector_omega_mass[i] = interval(omega_mass[i]); // par omega_mass
        }
      }
    }
  }

  if (etap_mass_flag) {
    if (mpchain) {
      etap_mass[0] = boltz * t_target / (p_freq_max*p_freq_max);
      ivector_etap_mass[0] = boltz * t_target / (p_freq_max*p_freq_max);

      for (int ich = 1; ich < mpchain; ich++) {
        etap_mass[ich] = boltz * t_target / (p_freq_max*p_freq_max);
        ivector_etap_mass[ich] = boltz * t_target / (p_freq_max*p_freq_max);
      }

      for (int ich = 1; ich < mpchain; ich++) {
        etap_dotdot[ich] = (etap_mass[ich-1]*etap_dot[ich-1]*etap_dot[ich-1] - boltz * t_target) / etap_mass[ich];
        ivector_etap_dotdot[ich] = interval(( interval(ivector_etap_mass[ich-1])*interval(ivector_etap_dot[ich-1])*interval(ivector_etap_dot[ich-1]) - boltz * t_target ) / interval(ivector_etap_mass[ich]));
      }
    }
  }

  kecurrent = 0.0;
  *interval_kecurrent = interval(0.0);

  for (i = 0; i < 3; i++){
    if (p_flag[i]) {
      kecurrent += omega_mass[i]*omega_dot[i]*omega_dot[i];
      *interval_kecurrent += interval(ivector_omega_mass[i])*interval(ivector_omega_dot[i])*interval(ivector_omega_dot[i]);
    }
  }

  if (pstyle == TRICLINIC) {
    for (i = 3; i < 6; i++) {
      if (p_flag[i]) {
        kecurrent += omega_mass[i]*omega_dot[i]*omega_dot[i];
        *interval_kecurrent += interval(ivector_omega_mass[i])*interval(ivector_omega_dot[i])*interval(ivector_omega_dot[i]);
      }
    }      
  }

  etap_dotdot[0] = (kecurrent - lkt_press)/etap_mass[0];
  ivector_eta_dotdot[0] = (*interval_kecurrent - lkt_press)/interval(ivector_etap_mass[0]); // par eta_dotdot

  double ncfac = 1.0/nc_pchain;
  for (int iloop = 0; iloop < nc_pchain; iloop++) {

    for (ich = mpchain-1; ich > 0; ich--) {
      expfac = exp(-ncfac*dt8*etap_dot[ich+1]);
      etap_dot[ich] *= expfac;
      etap_dot[ich] += etap_dotdot[ich] * ncfac*dt4;
      etap_dot[ich] *= pdrag_factor;
      etap_dot[ich] *= expfac;

      *interval_expfac = exp(-ncfac*dt8*interval(ivector_etap_dot[ich+1]));
      ivector_etap_dot[ich] *= (*interval_expfac);
      ivector_etap_dot[ich] = interval(ivector_etap_dot[ich]) + interval(ivector_etap_dotdot[ich]) * ncfac*dt4;
      ivector_etap_dot[ich] *= (*interval_pdrag_factor);
      ivector_etap_dot[ich] *= (*interval_expfac);
    }

    expfac = exp(-ncfac*dt8*etap_dot[1]);
    etap_dot[0] *= expfac;
    etap_dot[0] += etap_dotdot[0] * ncfac*dt4;
    etap_dot[0] *= pdrag_factor;
    etap_dot[0] *= expfac;

    *interval_expfac = exp(-ncfac*dt8*interval(ivector_etap_dot[1]));
    ivector_etap_dot[0] *= (*interval_expfac);
    ivector_etap_dot[0] = interval(ivector_etap_dot[0]) + interval(ivector_etap_dotdot[0]) * ncfac*dt4;
    ivector_etap_dot[0] *= (*interval_pdrag_factor);
    ivector_etap_dot[0] *= (*interval_expfac);

    for (ich = 0; ich < mpchain; ich++){      
      etap[ich] += ncfac*dthalf*etap_dot[ich];
      ivector_etap[ich] = interval(ivector_etap[ich]) + ncfac*dthalf*interval(ivector_etap_dot[ich]);
    }

    factor_etap = exp(-ncfac*dthalf*etap_dot[0]);
    *interval_factor_etap = exp(-ncfac*dthalf*interval(ivector_etap_dot[0]));

    for (i = 0; i < 3; i++) {
      if (p_flag[i]) {
        omega_dot[i] *= factor_etap;
        ivector_omega_dot[i] *= (*interval_factor_etap);
      }
    }

    if (pstyle == TRICLINIC) {
      for (i = 3; i < 6; i++) {
        if (p_flag[i]) {
          omega_dot[i] *= factor_etap;
          ivector_omega_dot[i] *= (*interval_factor_etap); 
        }
      }
    }

    kecurrent = 0.0;
    *interval_kecurrent = interval(0.0);

    for (i = 0; i < 3; i++) {
      if (p_flag[i]) { 
        kecurrent += omega_mass[i]*omega_dot[i]*omega_dot[i];
        *interval_kecurrent += interval(ivector_omega_mass[i])*interval(ivector_omega_dot[i])*interval(ivector_omega_dot[i]);
      }
    }

    if (pstyle == TRICLINIC) {
      for (i = 3; i < 6; i++) {
        if (p_flag[i]) {
          kecurrent += omega_mass[i]*omega_dot[i]*omega_dot[i];
          *interval_kecurrent += interval(ivector_omega_mass[i])*interval(ivector_omega_dot[i])*interval(ivector_omega_dot[i]);
        }
      }
    }

    etap_dotdot[0] = (kecurrent - lkt_press)/etap_mass[0];
    ivector_eta_dotdot[0] = (*interval_kecurrent - lkt_press)/interval(ivector_etap_mass[0]);

    etap_dot[0] *= expfac;
    etap_dot[0] += etap_dotdot[0] * ncfac*dt4;
    etap_dot[0] *= expfac;

    ivector_etap_dot[0] *= (*interval_expfac);
    ivector_etap_dot[0] = interval(ivector_etap_dot[0]) + interval(ivector_etap_dotdot[0]) * ncfac*dt4;
    ivector_etap_dot[0] *= (*interval_expfac);

    for (ich = 1; ich < mpchain; ich++) {
      expfac = exp(-ncfac*dt8*etap_dot[ich+1]);
      etap_dot[ich] *= expfac;
      etap_dotdot[ich] = (etap_mass[ich-1]*etap_dot[ich-1]*etap_dot[ich-1] - boltz*t_target) / etap_mass[ich];
      etap_dot[ich] += etap_dotdot[ich] * ncfac*dt4;
      etap_dot[ich] *= expfac;

      *interval_expfac = exp(-ncfac*dt8*interval(ivector_etap_dot[ich+1]));
      ivector_etap_dot[ich] *= (*interval_expfac);
      ivector_etap_dotdot[ich] = (interval(ivector_etap_mass[ich-1])*interval(ivector_etap_dot[ich-1])*interval(ivector_etap_dot[ich-1]) - boltz*t_target) / interval(ivector_etap_mass[ich]);
      ivector_etap_dot[ich] = interval(ivector_etap_dot[ich]) + interval(ivector_etap_dotdot[ich]) * ncfac*dt4;
      ivector_etap_dot[ich] *= (*interval_expfac);

      // begin debug
      // status: OK
      // std::cout << "nhc_press_integrate(): expfac = " << expfac << std::endl;
      // std::cout << "nhc_press_integrate(): *interval_expfac = " << *interval_expfac << std::endl;

      // std::cout << "etap_dot[" << ich << "] = " << etap_dot[ich] << std::endl;
      // std::cout << "ivector_etap_dot[" << ich << "] = " << ivector_etap_dot[ich] << std::endl;

      // std::cout << "etap_dotdot[" << ich << "] = " << etap_dotdot[ich] << std::endl;
      // std::cout << "ivector_etap_dotdot[" << ich << "] = " << ivector_etap_dotdot[ich] << std::endl;

      // end debug
    }
  }
}

/* ----------------------------------------------------------------------
   perform half-step barostat scaling of velocities
-----------------------------------------------------------------------*/

void FixNH::nh_v_press()
{
  double factor[3];
  double **v = atom->v;
  double **vrad = atom->vrad;
  double **vub  = atom->vub;
  double **vlb  = atom->vlb;

  int *mask = atom->mask;
  int nlocal = atom->nlocal;
  if (igroup == atom->firstgroup) nlocal = atom->nfirst;

  factor[0] = exp(-dt4*(omega_dot[0]+mtk_term2));
  factor[1] = exp(-dt4*(omega_dot[1]+mtk_term2));
  factor[2] = exp(-dt4*(omega_dot[2]+mtk_term2));

  ivector_factor[0] = exp(-dt4*( interval(ivector_omega_dot[0]) + (*interval_mtk_term2) ) );
  ivector_factor[1] = exp(-dt4*( interval(ivector_omega_dot[1]) + (*interval_mtk_term2) ) );
  ivector_factor[2] = exp(-dt4*( interval(ivector_omega_dot[2]) + (*interval_mtk_term2) ) );

  // begin debug
  // std::cout << "FixNH::nh_v_press() running ... " << std::endl;
  // status: same, except for accuracy problems
  // for (int i = 0; i < 3; i++) {
  //   std::cout << "factor[" << i << "] = " << factor[i] << std::endl;
  //   std::cout << "ivector_factor[" << i << "] = " << ivector_factor[i] << std::endl;
  // }
  // end debug

  if (which == NOBIAS) {
    for (int i = 0; i < nlocal; i++) {
      if (mask[i] & groupbit) {

        // begin debug
        // for (int ii = 0; ii < 3; i++) {
        //   std::cout << "v[" << i << "][" << ii << "] = " << v[i][ii] << std::endl;
        //   std::cout << "vlb[" << i << "][" << ii << "] = " << vlb[i][ii] << std::endl;
        //   std::cout << "vub[" << i << "][" << ii << "] = " << vub[i][ii] << std::endl;
        // }
        // end debug

        v[i][0] *= factor[0];
        v[i][1] *= factor[1];
        v[i][2] *= factor[2];


        // par

        interval tmpv0 = interval(vlb[i][0],vub[i][0]);
        interval tmpv1 = interval(vlb[i][1],vub[i][1]);
        interval tmpv2 = interval(vlb[i][2],vub[i][2]);

        tmpv0 *= interval(ivector_factor[0]);
        tmpv1 *= interval(ivector_factor[1]);
        tmpv2 *= interval(ivector_factor[2]);

        // update
        vlb[i][0] = _double(tmpv0.inf); vub[i][0] = _double(tmpv0.sup);
        vlb[i][1] = _double(tmpv1.inf); vub[i][1] = _double(tmpv1.sup);
        vlb[i][2] = _double(tmpv2.inf); vub[i][2] = _double(tmpv2.sup);

        // begin debug
        // status: bug = yes -- tmpv0 = tmpv1 = tmpv2 = 0
        // for (int ii = 0; ii < 3; ii++) {
        //   std::cout << "v[" << i << "][" << ii << "] = " << v[i][ii] << std::endl;
        //   if (ii == 0) std::cout << "tmpv0 = " << tmpv0 << std::endl;
        //   if (ii == 1) std::cout << "tmpv1 = " << tmpv1 << std::endl;
        //   if (ii == 2) std::cout << "tmpv2 = " << tmpv2 << std::endl;
        // }
        // end debug

        if (pstyle == TRICLINIC) {
          v[i][0] += -dthalf*(v[i][1]*omega_dot[5] + v[i][2]*omega_dot[4]);
          v[i][1] += -dthalf*v[i][2]*omega_dot[3];

          // par
          // need to check if the interval stills OK after the scope - otherwise need to create instance of objects // yes - OK if scope opens
          tmpv0 += -dthalf*( tmpv1 * interval(ivector_omega_dot[5]) + tmpv2 * interval(ivector_omega_dot[4]) );
          tmpv1 += -dthalf*tmpv2*interval(ivector_omega_dot[3]);

          // update
          vlb[i][0] = _double(tmpv0.inf); vub[i][0] = _double(tmpv0.sup);
          vlb[i][1] = _double(tmpv1.inf); vub[i][1] = _double(tmpv1.sup);
          vlb[i][2] = _double(tmpv2.inf); vub[i][2] = _double(tmpv2.sup);
        }

        v[i][0] *= factor[0];
        v[i][1] *= factor[1];
        v[i][2] *= factor[2];

        // par

        tmpv0 *= interval(ivector_factor[0]);
        tmpv1 *= interval(ivector_factor[1]);
        tmpv2 *= interval(ivector_factor[2]);

        // update
        vlb[i][0] = _double(tmpv0.inf); vub[i][0] = _double(tmpv0.sup);
        vlb[i][1] = _double(tmpv1.inf); vub[i][1] = _double(tmpv1.sup);
        vlb[i][2] = _double(tmpv2.inf); vub[i][2] = _double(tmpv2.sup);

        // begin debug
        // std::cout << "NOBIAS - nh_v_press() is running... " << std::endl; // yes - running
        // for (int ii = 0; ii < 3; i++) {
        //   std::cout << "v[" << i << "][" << ii << "] = " << v[i][ii] << std::endl;
        //   std::cout << "vlb[" << i << "][" << ii << "] = " << vlb[i][ii] << std::endl;
        //   std::cout << "vub[" << i << "][" << ii << "] = " << vub[i][ii] << std::endl;
        // }
        // end debug

      }
    }
  } else if (which == BIAS) {
    for (int i = 0; i < nlocal; i++) {
      if (mask[i] & groupbit) {
        temperature->remove_bias(i,v[i]);
        temperature->remove_bias(i,vlb[i]);
        temperature->remove_bias(i,vub[i]);

        v[i][0] *= factor[0];
        v[i][1] *= factor[1];
        v[i][2] *= factor[2];

        // begin debug
        // std::cout << "tmpv0" << tmpv0 << std::endl; // yes - outside the scope the interval objects are destroyed
        // end debug

        // begin debug
        // status: not running here
        // std::cout << "running here ... " << std::endl;
        // end debug

        // par

        interval tmpv0 = interval(vlb[i][0],vub[i][0]);
        interval tmpv1 = interval(vlb[i][1],vub[i][1]);
        interval tmpv2 = interval(vlb[i][2],vub[i][2]);

        tmpv0 *= interval(ivector_factor[0]);
        tmpv1 *= interval(ivector_factor[1]);
        tmpv2 *= interval(ivector_factor[2]);

        // update
        vlb[i][0] = _double(tmpv0.inf); vub[i][0] = _double(tmpv0.sup);
        vlb[i][1] = _double(tmpv1.inf); vub[i][1] = _double(tmpv1.sup);
        vlb[i][2] = _double(tmpv2.inf); vub[i][2] = _double(tmpv2.sup);

        if (pstyle == TRICLINIC) {
          v[i][0] += -dthalf*(v[i][1]*omega_dot[5] + v[i][2]*omega_dot[4]);
          v[i][1] += -dthalf*v[i][2]*omega_dot[3];

          // par
          tmpv0 += -dthalf*(tmpv1*interval(ivector_omega_dot[5]) + tmpv2*interval(ivector_omega_dot[4]) );
          tmpv1 += -dthalf*(tmpv2)*interval(ivector_omega_dot[3]);

          // update
          vlb[i][0] = _double(tmpv0.inf); vub[i][0] = _double(tmpv0.sup);
          vlb[i][1] = _double(tmpv1.inf); vub[i][1] = _double(tmpv1.sup);
          vlb[i][2] = _double(tmpv2.inf); vub[i][2] = _double(tmpv2.sup);

        }
        v[i][0] *= factor[0];
        v[i][1] *= factor[1];
        v[i][2] *= factor[2];

        // par
        tmpv0 *= interval(ivector_factor[0]);
        tmpv1 *= interval(ivector_factor[1]);
        tmpv2 *= interval(ivector_factor[2]);

        // update

        vlb[i][0] = _double(tmpv0.inf); vub[i][0] = _double(tmpv0.sup);
        vlb[i][1] = _double(tmpv1.inf); vub[i][1] = _double(tmpv1.sup);
        vlb[i][1] = _double(tmpv2.inf); vub[i][2] = _double(tmpv2.sup);

        temperature->restore_bias(i,v[i]);
        temperature->restore_bias(i,vlb[i]);
        temperature->restore_bias(i,vub[i]);
      }
    }
  }
}

/* ----------------------------------------------------------------------
   perform half-step update of velocities
-----------------------------------------------------------------------*/

void FixNH::nve_v()
{
  double dtfm;
  double **v = atom->v;
  double **vub = atom->vub;
  double **vlb = atom->vlb;
  double **f = atom->f;
  double **fub = atom->fub;
  double **flb = atom->flb;
  double *rmass = atom->rmass;
  double *mass = atom->mass;
  int *type = atom->type;
  int *mask = atom->mask;
  int nlocal = atom->nlocal;
  if (igroup == atom->firstgroup) nlocal = atom->nfirst;

  if (rmass) {
    for (int i = 0; i < nlocal; i++) {
      if (mask[i] & groupbit) {
        init_sync(i);

        dtfm = dtf / rmass[i];
        v[i][0] += dtfm*f[i][0];
        v[i][1] += dtfm*f[i][1];
        v[i][2] += dtfm*f[i][2];

        // par -- separating lower and upper bounds because dtfm is constant - it is equivalent to interval concept

        vlb[i][0] += dtfm*flb[i][0];
        vlb[i][1] += dtfm*flb[i][1];
        vlb[i][2] += dtfm*flb[i][2];

        vub[i][0] += dtfm*fub[i][0];
        vub[i][1] += dtfm*fub[i][1];
        vub[i][2] += dtfm*fub[i][2];
      }
    }
  } else {
    for (int i = 0; i < nlocal; i++) {
      if (mask[i] & groupbit) {
        init_sync(i);

        dtfm = dtf / mass[type[i]];


        // begin debug
        // if (update->ntimestep > (*sync_step)) {
        //   for (int j = 0; j < 3; j++) {
        //     std::cout << "f["   << i << "][" << j << "] = " << f[i][j]   << std::endl; 
        //     std::cout << "fub[" << i << "][" << j << "] = " << fub[i][j] << std::endl; 
        //     std::cout << "flb[" << i << "][" << j << "] = " << flb[i][j] << std::endl; 
        //   }
        // }
        // end debug

        v[i][0] += dtfm*f[i][0];
        v[i][1] += dtfm*f[i][1];
        v[i][2] += dtfm*f[i][2];

        // par -- separating lower and upper bounds because dtfm is constant - it is equivalent to interval concept

        vlb[i][0] += dtfm*flb[i][0];
        vlb[i][1] += dtfm*flb[i][1];
        vlb[i][2] += dtfm*flb[i][2];

        vub[i][0] += dtfm*fub[i][0];
        vub[i][1] += dtfm*fub[i][1];
        vub[i][2] += dtfm*fub[i][2];

        // begin debug
        // status v != (vlb = vub) -- moreover, vlb and vub very very small
        // status: f = flb = fub
        // if (update->ntimestep > (*sync_step)) {
        //   for (int j = 0; j < 3; j++) {
        //     std::cout << "v[" << i << "][" << j << "][" << update->ntimestep << "] = " << v[i][j] << std::endl;
        //     std::cout << "vlb[" << i << "][" << j << "][" << update->ntimestep << "] = " << vlb[i][j] << std::endl;
        //     std::cout << "vub[" << i << "][" << j << "][" << update->ntimestep << "] = " << vub[i][j] << std::endl;
        //     std::cout << "f["   << i << "][" << j << "][" << update->ntimestep << "] = " << f[i][j] << std::endl;
        //     std::cout << "flb[" << i << "][" << j << "][" << update->ntimestep << "] = " << flb[i][j] << std::endl;
        //     std::cout << "fub[" << i << "][" << j << "][" << update->ntimestep << "] = " << fub[i][j] << std::endl;
        //   }
        // }
        // end debug
      }
    }
  }
}

/* ----------------------------------------------------------------------
   perform full-step update of positions
-----------------------------------------------------------------------*/

void FixNH::nve_x()
{
  double **x = atom->x;
  double **xlb = atom->xlb;
  double **xub = atom->xub;
  double **v = atom->v;
  double **vub = atom->vub;
  double **vlb = atom->vlb;
  int *mask = atom->mask;
  int nlocal = atom->nlocal;
  if (igroup == atom->firstgroup) nlocal = atom->nfirst;

  // x update by full step only for atoms in group

  for (int i = 0; i < nlocal; i++) {
    if (mask[i] & groupbit) {

      // begin debug
      // std::cout << "update->ntimestep = " << update->ntimestep << std::endl;
      // end debug

      init_sync(i);

      x[i][0] += dtv * v[i][0];
      x[i][1] += dtv * v[i][1];
      x[i][2] += dtv * v[i][2];

      // par

      xlb[i][0] += dtv * vlb[i][0];
      xlb[i][1] += dtv * vlb[i][1];
      xlb[i][2] += dtv * vlb[i][2];

      xub[i][0] += dtv * vub[i][0];
      xub[i][1] += dtv * vub[i][1];
      xub[i][2] += dtv * vub[i][2];

      // begin debug
      // note: put in update->ntimestep
      // if (update->ntimestep > (*sync_step)) {
      //   for (int j = 0; j < 3; j++) {
      //     std::cout << "v["   << i << "][" << j << "][" << update->ntimestep << "] = " << v[i][j]   << std::endl;
      //     std::cout << "vlb[" << i << "][" << j << "][" << update->ntimestep << "] = " << vlb[i][j] << std::endl;
      //     std::cout << "vub[" << i << "][" << j << "][" << update->ntimestep << "] = " << vub[i][j] << std::endl;
      //     std::cout << "x["   << i << "][" << j << "][" << update->ntimestep << "] = " << x[i][j]   << std::endl;
      //     std::cout << "xlb[" << i << "][" << j << "][" << update->ntimestep << "] = " << xlb[i][j] << std::endl;
      //     std::cout << "xub[" << i << "][" << j << "][" << update->ntimestep << "] = " << xub[i][j] << std::endl;
      //   }
      // }
      // end debug
    }
  }
}

/* ----------------------------------------------------------------------
   perform half-step thermostat scaling of velocities
-----------------------------------------------------------------------*/

void FixNH::nh_v_temp()
{
  double **v = atom->v;
  double **vrad = atom->vrad;
  double **vub = atom->vub;
  double **vlb = atom->vlb;
  int *mask = atom->mask;
  int nlocal = atom->nlocal;
  if (igroup == atom->firstgroup) nlocal = atom->nfirst;

  if (which == NOBIAS) {
    for (int i = 0; i < nlocal; i++) {
      if (mask[i] & groupbit) {
        v[i][0] *= factor_eta;
        v[i][1] *= factor_eta;
        v[i][2] *= factor_eta;

        interval tmp0 = interval(vlb[i][0], vub[i][0]) * (*interval_factor_eta);
        interval tmp1 = interval(vlb[i][1], vub[i][1]) * (*interval_factor_eta);
        interval tmp2 = interval(vlb[i][2], vub[i][2]) * (*interval_factor_eta);

        vlb[i][0] = _double(tmp0.inf); vub[i][0] = _double(tmp0.sup);
        vlb[i][1] = _double(tmp1.inf); vub[i][1] = _double(tmp1.sup);
        vlb[i][2] = _double(tmp2.inf); vub[i][2] = _double(tmp2.sup);
        // std::cout << "this example is nobiased ... " << std::endl; // debug: yes - nobias

        // begin debug
        // status: factor_eta = 1.00001; *interval_factor_eta = [  0.999949,  0.999950]
        // if (update->ntimestep > (*sync_step)) {
        //   std::cout << "factor_eta = " << factor_eta << std::endl;
        //   std::cout << "*interval_factor_eta = " << *interval_factor_eta << std::endl;
        // }
        // end debug

        // begin debug
        // status: OK
        // std::cout << "tmp0 = " << tmp0 << std::endl;
        // std::cout << "tmp1 = " << tmp1 << std::endl;
        // std::cout << "tmp2 = " << tmp2 << std::endl;
        // end debug

        // begin debug
        // status: OK
        // std::cout << "vlb[" << i << "][0] = " << vlb[i][0] << std::endl;
        // std::cout << "vlb[" << i << "][1] = " << vlb[i][1] << std::endl;
        // std::cout << "vlb[" << i << "][2] = " << vlb[i][2] << std::endl;
        // end debug

      }
    }
  } else if (which == BIAS) {
    for (int i = 0; i < nlocal; i++) {
      if (mask[i] & groupbit) {
        temperature->remove_bias(i,v[i]);
        temperature->remove_bias(i,vlb[i]);
        temperature->remove_bias(i,vub[i]);
        v[i][0] *= factor_eta;
        v[i][1] *= factor_eta;
        v[i][2] *= factor_eta;

        interval tmp0 = interval(vlb[i][0], vub[i][0]) * (*interval_factor_eta);
        interval tmp1 = interval(vlb[i][1], vub[i][1]) * (*interval_factor_eta);
        interval tmp2 = interval(vlb[i][2], vub[i][2]) * (*interval_factor_eta);

        vlb[i][0] = _double(tmp0.inf); vub[i][0] = _double(tmp0.sup);
        vlb[i][1] = _double(tmp1.inf); vub[i][1] = _double(tmp1.sup);
        vlb[i][2] = _double(tmp2.inf); vub[i][2] = _double(tmp2.sup);

        temperature->restore_bias(i,v[i]);
        temperature->restore_bias(i,vlb[i]);
        temperature->restore_bias(i,vub[i]);
      }
    }
  }
}

/* ----------------------------------------------------------------------
   compute sigma tensor
   needed whenever p_target or h0_inv changes
-----------------------------------------------------------------------*/

void FixNH::compute_sigma()
{
  // if nreset_h0 > 0, reset vol0 and h0_inv
  // every nreset_h0 timesteps
  // std::cout << "running compute_sigma() ... " << std::endl; // debug: yes - calling this function every timestep

  if (nreset_h0 > 0) {
    int delta = update->ntimestep - update->beginstep;
    if (delta % nreset_h0 == 0) {
      if (dimension == 3) vol0 = domain->xprd * domain->yprd * domain->zprd;
      else vol0 = domain->xprd * domain->yprd;
      h0_inv[0] = domain->h_inv[0];
      h0_inv[1] = domain->h_inv[1];
      h0_inv[2] = domain->h_inv[2];
      h0_inv[3] = domain->h_inv[3];
      h0_inv[4] = domain->h_inv[4];
      h0_inv[5] = domain->h_inv[5];
    }
  }

  // generate upper-triangular half of
  // sigma = vol0*h0inv*(p_target-p_hydro)*h0inv^t
  // units of sigma are are PV/L^2 e.g. atm.A
  //
  // [ 0 5 4 ]   [ 0 5 4 ] [ 0 5 4 ] [ 0 - - ]
  // [ 5 1 3 ] = [ - 1 3 ] [ 5 1 3 ] [ 5 1 - ]
  // [ 4 3 2 ]   [ - - 2 ] [ 4 3 2 ] [ 4 3 2 ]

  sigma[0] =
    vol0*(h0_inv[0]*((p_target[0]-p_hydro)*h0_inv[0] +
                     p_target[5]*h0_inv[5]+p_target[4]*h0_inv[4]) +
          h0_inv[5]*(p_target[5]*h0_inv[0] +
                     (p_target[1]-p_hydro)*h0_inv[5]+p_target[3]*h0_inv[4]) +
          h0_inv[4]*(p_target[4]*h0_inv[0]+p_target[3]*h0_inv[5] +
                     (p_target[2]-p_hydro)*h0_inv[4]));
  sigma[1] =
    vol0*(h0_inv[1]*((p_target[1]-p_hydro)*h0_inv[1] +
                     p_target[3]*h0_inv[3]) +
          h0_inv[3]*(p_target[3]*h0_inv[1] +
                     (p_target[2]-p_hydro)*h0_inv[3]));
  sigma[2] =
    vol0*(h0_inv[2]*((p_target[2]-p_hydro)*h0_inv[2]));
  sigma[3] =
    vol0*(h0_inv[1]*(p_target[3]*h0_inv[2]) +
          h0_inv[3]*((p_target[2]-p_hydro)*h0_inv[2]));
  sigma[4] =
    vol0*(h0_inv[0]*(p_target[4]*h0_inv[2]) +
          h0_inv[5]*(p_target[3]*h0_inv[2]) +
          h0_inv[4]*((p_target[2]-p_hydro)*h0_inv[2]));
  sigma[5] =
    vol0*(h0_inv[0]*(p_target[5]*h0_inv[1]+p_target[4]*h0_inv[3]) +
          h0_inv[5]*((p_target[1]-p_hydro)*h0_inv[1]+p_target[3]*h0_inv[3]) +
          h0_inv[4]*(p_target[3]*h0_inv[1]+(p_target[2]-p_hydro)*h0_inv[3]));

  // par sigma
  ivector_sigma[0] =
    vol0*(h0_inv[0]*((interval(ivector_p_target[0])-(*interval_p_hydro))*h0_inv[0] +
                     interval(ivector_p_target[5])*h0_inv[5]+interval(ivector_p_target[4])*h0_inv[4]) +
          h0_inv[5]*(interval(ivector_p_target[5])*h0_inv[0] +
                     (interval(ivector_p_target[1])-(*interval_p_hydro))*h0_inv[5]+interval(ivector_p_target[3])*h0_inv[4]) +
          h0_inv[4]*(interval(ivector_p_target[4])*h0_inv[0]+interval(ivector_p_target[3])*h0_inv[5] +
                     (interval(ivector_p_target[2])-(*interval_p_hydro))*h0_inv[4]));
  ivector_sigma[1] =
    vol0*(h0_inv[1]*((interval(ivector_p_target[1])-(*interval_p_hydro))*h0_inv[1] +
                     interval(ivector_p_target[3])*h0_inv[3]) +
          h0_inv[3]*(interval(ivector_p_target[3])*h0_inv[1] +
                     (interval(ivector_p_target[2])-(*interval_p_hydro))*h0_inv[3]));
  ivector_sigma[2] =
    vol0*(h0_inv[2]*((interval(ivector_p_target[2])-(*interval_p_hydro))*h0_inv[2]));
  ivector_sigma[3] =
    vol0*(h0_inv[1]*(interval(ivector_p_target[3])*h0_inv[2]) +
          h0_inv[3]*((interval(ivector_p_target[2])-(*interval_p_hydro))*h0_inv[2]));
  ivector_sigma[4] =
    vol0*(h0_inv[0]*(interval(ivector_p_target[4])*h0_inv[2]) +
          h0_inv[5]*(interval(ivector_p_target[3])*h0_inv[2]) +
          h0_inv[4]*((interval(ivector_p_target[2])-(*interval_p_hydro))*h0_inv[2]));
  ivector_sigma[5] =
    vol0*(h0_inv[0]*(interval(ivector_p_target[5])*h0_inv[1]+interval(ivector_p_target[4])*h0_inv[3]) +
          h0_inv[5]*((interval(ivector_p_target[1])-(*interval_p_hydro))*h0_inv[1]+interval(ivector_p_target[3])*h0_inv[3]) +
          h0_inv[4]*(interval(ivector_p_target[3])*h0_inv[1]+(interval(ivector_p_target[2])-(*interval_p_hydro))*h0_inv[3]));

    // begin debug
    // status: not used
    // for (int j = 0; j < 6; j++) {
    //   std::cout << "ivector_sigma[" << j << "] = " << ivector_sigma[j] << std::endl;
    // }
    // end debug

}

/* ----------------------------------------------------------------------
   compute strain energy
-----------------------------------------------------------------------*/

double FixNH::compute_strain_energy()
{
  // compute strain energy = 0.5*Tr(sigma*h*h^t) in energy units

  double* h = domain->h;
  double d0,d1,d2;

  d0 =
    sigma[0]*(h[0]*h[0]+h[5]*h[5]+h[4]*h[4]) +
    sigma[5]*(          h[1]*h[5]+h[3]*h[4]) +
    sigma[4]*(                    h[2]*h[4]);
  d1 =
    sigma[5]*(          h[5]*h[1]+h[4]*h[3]) +
    sigma[1]*(          h[1]*h[1]+h[3]*h[3]) +
    sigma[3]*(                    h[2]*h[3]);
  d2 =
    sigma[4]*(                    h[4]*h[2]) +
    sigma[3]*(                    h[3]*h[2]) +
    sigma[2]*(                    h[2]*h[2]);

  double energy = 0.5*(d0+d1+d2)/nktv2p;
  return energy;
}

/* ----------------------------------------------------------------------
   compute deviatoric barostat force = h*sigma*h^t
-----------------------------------------------------------------------*/

void FixNH::compute_deviatoric()
{
  // generate upper-triangular part of h*sigma*h^t
  // units of fdev are are PV, e.g. atm*A^3
  // [ 0 5 4 ]   [ 0 5 4 ] [ 0 5 4 ] [ 0 - - ]
  // [ 5 1 3 ] = [ - 1 3 ] [ 5 1 3 ] [ 5 1 - ]
  // [ 4 3 2 ]   [ - - 2 ] [ 4 3 2 ] [ 4 3 2 ]

  // std::cout << "running ... " << std::endl; // not used

  double* h = domain->h;

  fdev[0] =
    h[0]*(sigma[0]*h[0]+sigma[5]*h[5]+sigma[4]*h[4]) +
    h[5]*(sigma[5]*h[0]+sigma[1]*h[5]+sigma[3]*h[4]) +
    h[4]*(sigma[4]*h[0]+sigma[3]*h[5]+sigma[2]*h[4]);
  fdev[1] =
    h[1]*(              sigma[1]*h[1]+sigma[3]*h[3]) +
    h[3]*(              sigma[3]*h[1]+sigma[2]*h[3]);
  fdev[2] =
    h[2]*(                            sigma[2]*h[2]);
  fdev[3] =
    h[1]*(                            sigma[3]*h[2]) +
    h[3]*(                            sigma[2]*h[2]);
  fdev[4] =
    h[0]*(                            sigma[4]*h[2]) +
    h[5]*(                            sigma[3]*h[2]) +
    h[4]*(                            sigma[2]*h[2]);
  fdev[5] =
    h[0]*(              sigma[5]*h[1]+sigma[4]*h[3]) +
    h[5]*(              sigma[1]*h[1]+sigma[3]*h[3]) +
    h[4]*(              sigma[3]*h[1]+sigma[2]*h[3]);

  // par

  ivector_fdev[0] =
    h[0]*(interval(ivector_sigma[0])*h[0]+interval(ivector_sigma[5])*h[5]+interval(ivector_sigma[4])*h[4]) +
    h[5]*(interval(ivector_sigma[5])*h[0]+interval(ivector_sigma[1])*h[5]+interval(ivector_sigma[3])*h[4]) +
    h[4]*(interval(ivector_sigma[4])*h[0]+interval(ivector_sigma[3])*h[5]+interval(ivector_sigma[2])*h[4]);
  ivector_fdev[1] =
    h[1]*(              interval(ivector_sigma[1])*h[1]+interval(ivector_sigma[3])*h[3]) +
    h[3]*(              interval(ivector_sigma[3])*h[1]+interval(ivector_sigma[2])*h[3]);
  ivector_fdev[2] =
    h[2]*(                            interval(ivector_sigma[2])*h[2]);
  ivector_fdev[3] =
    h[1]*(                            interval(ivector_sigma[3])*h[2]) +
    h[3]*(                            interval(ivector_sigma[2])*h[2]);
  ivector_fdev[4] =
    h[0]*(                            interval(ivector_sigma[4])*h[2]) +
    h[5]*(                            interval(ivector_sigma[3])*h[2]) +
    h[4]*(                            interval(ivector_sigma[2])*h[2]);
  ivector_fdev[5] =
    h[0]*(              interval(ivector_sigma[5])*h[1]+interval(ivector_sigma[4])*h[3]) +
    h[5]*(              interval(ivector_sigma[1])*h[1]+interval(ivector_sigma[3])*h[3]) +
    h[4]*(              interval(ivector_sigma[3])*h[1]+interval(ivector_sigma[2])*h[3]);

  // begin debug
  // status: not used
  // for (int j = 0; j < 6; j++) {
  //   std::cout << "ivector_fdev[" << j << "] = " << ivector_fdev[j] << std::endl;
  // }
  // end debug

}

/* ----------------------------------------------------------------------
   compute target temperature and kinetic energy
-----------------------------------------------------------------------*/

void FixNH::compute_temp_target()
{
  double delta = update->ntimestep - update->beginstep;
  if (delta != 0.0) delta /= update->endstep - update->beginstep;

  t_target = t_start + delta * (t_stop-t_start);
  ke_target = tdof * boltz * t_target;

  *interval_t_target = (*interval_t_start) + delta * ( (*interval_t_stop) - (*interval_t_start) );
  *interval_ke_target = tdof * boltz * (*interval_t_target);
}

/* ----------------------------------------------------------------------
   compute hydrostatic target pressure
-----------------------------------------------------------------------*/

void FixNH::compute_press_target()
{
  double delta = update->ntimestep - update->beginstep;
  if (delta != 0.0) delta /= update->endstep - update->beginstep;

  p_hydro = 0.0;
  // *interval_p_hydro = interval(0.0);

  for (int i = 0; i < 3; i++)
    if (p_flag[i]) {
      p_target[i] = p_start[i] + delta * (p_stop[i]-p_start[i]);
      p_hydro += p_target[i];

      ivector_p_target[i] = interval(ivector_p_start[i]) + delta * (interval(ivector_p_stop[i]) - interval(ivector_p_start[i]));
      *interval_p_hydro += interval(ivector_p_target[i]);
    }

  p_hydro /= pdim;
  *interval_p_hydro /= pdim;

  if (pstyle == TRICLINIC) {
    for (int i = 3; i < 6; i++) {
      p_target[i] = p_start[i] + delta * (p_stop[i]-p_start[i]);
      ivector_p_target[i] = interval(ivector_p_start[i]) + delta * (interval(ivector_p_stop[i]) - interval(ivector_p_start[i]));
    }
  }
  // if deviatoric, recompute sigma each time p_target changes

  // begin debug
  // status: deviatory_flag = 0
  // std::cout << "deviatoric_flag = " << deviatoric_flag << std::endl;
  // end debug

  if (deviatoric_flag) compute_sigma();
}

/* ----------------------------------------------------------------------
   update omega_dot, omega
-----------------------------------------------------------------------*/

void FixNH::nh_omega_dot()
{
  double f_omega,volume;

  if (dimension == 3) volume = domain->xprd*domain->yprd*domain->zprd;
  else volume = domain->xprd*domain->yprd;

  if (deviatoric_flag) compute_deviatoric();

  mtk_term1 = 0.0;
  *interval_mtk_term1 = interval(0.0);


  if (mtk_flag) {
    if (pstyle == ISO) {
      mtk_term1 = tdof * boltz * t_current;
      mtk_term1 /= pdim * atom->natoms;

      *interval_mtk_term1 = tdof * boltz * (*interval_t_current);
      *interval_mtk_term1 /= pdim * atom->natoms;
    } else {

      double *mvv_current = temperature->vector;
      double *mvv_current_ub = temperature->vector_ub;
      double *mvv_current_lb = temperature->vector_lb;

      for (int i = 0; i < 3; i++) {
        if (p_flag[i]) {
          mtk_term1 += mvv_current[i];
          *interval_mtk_term1 += interval(mvv_current_lb[i],mvv_current_ub[i]);
        }

        mtk_term1 /= pdim * atom->natoms;
        *interval_mtk_term1 /= pdim * atom->natoms;
      }      
    }
  }

  for (int i = 0; i < 3; i++)
    if (p_flag[i]) {
      f_omega = (p_current[i]-p_hydro)*volume / (omega_mass[i] * nktv2p) + mtk_term1 / omega_mass[i];
      *interval_f_omega = (interval(ivector_p_current[i])-(*interval_p_hydro))*volume / (interval(ivector_omega_mass[i]) * nktv2p) + (*interval_mtk_term1) / interval(ivector_omega_mass[i]);

      if (deviatoric_flag) {
        f_omega -= fdev[i]/(omega_mass[i] * nktv2p);
        *interval_f_omega -= interval(ivector_fdev[i]) / ( interval(ivector_omega_mass[i]) * nktv2p);
      }

      omega_dot[i] += f_omega*dthalf;
      omega_dot[i] *= pdrag_factor;

      ivector_omega_dot[i] = interval(ivector_omega_dot[i]) + (*interval_f_omega)*dthalf;
      ivector_omega_dot[i] *= (*interval_pdrag_factor);
    }

  mtk_term2 = 0.0;
  *interval_mtk_term2 = interval(0.0);


  if (mtk_flag) {
    for (int i = 0; i < 3; i++) {
      if (p_flag[i]) {
        mtk_term2 += omega_dot[i];
        *interval_mtk_term2 += interval(ivector_omega_dot[i]);
      }
    }

    mtk_term2 /= pdim * atom->natoms;
    *interval_mtk_term2 /= pdim * atom->natoms;

  }

  if (pstyle == TRICLINIC) {
    for (int i = 3; i < 6; i++) {
      if (p_flag[i]) {
        f_omega = p_current[i]*volume/(omega_mass[i] * nktv2p);
        *interval_f_omega = interval(ivector_p_current[i])*volume/(interval(ivector_omega_mass[i]) * nktv2p);

        if (deviatoric_flag) {
          f_omega -= fdev[i]/(omega_mass[i] * nktv2p);
          *interval_f_omega -= interval(ivector_fdev[i])/( interval(ivector_omega_mass[i]) * nktv2p);
        }

        omega_dot[i] += f_omega*dthalf;
        omega_dot[i] *= pdrag_factor;

        ivector_omega_dot[i] = interval(ivector_omega_dot[i]) + (*interval_f_omega)*dthalf;
        ivector_omega_dot[i] *= (*interval_pdrag_factor);
      }
    }
  }
}

/* ----------------------------------------------------------------------
  if any tilt ratios exceed limits, set flip = 1 and compute new tilt values
  do not flip in x or y if non-periodic (can tilt but not flip)
    this is b/c the box length would be changed (dramatically) by flip
  if yz tilt exceeded, adjust C vector by one B vector
  if xz tilt exceeded, adjust C vector by one A vector
  if xy tilt exceeded, adjust B vector by one A vector
  check yz first since it may change xz, then xz check comes after
  if any flip occurs, create new box in domain
  image_flip() adjusts image flags due to box shape change induced by flip
  remap() puts atoms outside the new box back into the new box
  perform irregular on atoms in lamda coords to migrate atoms to new procs
  important that image_flip comes before remap, since remap may change
    image flags to new values, making eqs in doc of Domain:image_flip incorrect
------------------------------------------------------------------------- */

void FixNH::pre_exchange()
{
  double xprd = domain->xprd;
  double yprd = domain->yprd;

  // flip is only triggered when tilt exceeds 0.5 by DELTAFLIP
  // this avoids immediate re-flipping due to tilt oscillations

  double xtiltmax = (0.5+DELTAFLIP)*xprd;
  double ytiltmax = (0.5+DELTAFLIP)*yprd;

  int flipxy,flipxz,flipyz;
  flipxy = flipxz = flipyz = 0;

  if (domain->yperiodic) {
    if (domain->yz < -ytiltmax) {
      domain->yz += yprd;
      domain->xz += domain->xy;
      flipyz = 1;
    } else if (domain->yz >= ytiltmax) {
      domain->yz -= yprd;
      domain->xz -= domain->xy;
      flipyz = -1;
    }
  }

  if (domain->xperiodic) {
    if (domain->xz < -xtiltmax) {
      domain->xz += xprd;
      flipxz = 1;
    } else if (domain->xz >= xtiltmax) {
      domain->xz -= xprd;
      flipxz = -1;
    }
    if (domain->xy < -xtiltmax) {
      domain->xy += xprd;
      flipxy = 1;
    } else if (domain->xy >= xtiltmax) {
      domain->xy -= xprd;
      flipxy = -1;
    }
  }

  int flip = 0;
  if (flipxy || flipxz || flipyz) flip = 1;

  if (flip) {
    domain->set_global_box();
    domain->set_local_box();

    domain->image_flip(flipxy,flipxz,flipyz);

    double **x = atom->x;
    imageint *image = atom->image;
    int nlocal = atom->nlocal;
    for (int i = 0; i < nlocal; i++) domain->remap(x[i],image[i]);

    domain->x2lamda(atom->nlocal);
    irregular->migrate_atoms();
    domain->lamda2x(atom->nlocal);
  }
}

/* ----------------------------------------------------------------------
  init_sync: sync vub[i], vlb[i] and v[i] if update->ntimestep < a constant
------------------------------------------------------------------------- */
void FixNH::init_sync(int i) 
{
  double **x = atom->x;
  double **xub = atom->xub;
  double **xlb = atom->xlb;
  double **v = atom->v;
  double **vub = atom->vub;
  double **vlb = atom->vlb;

  double *h = domain->h;
  // sync when update->ntimestep < a designated number
  if (update->ntimestep < (*sync_step)) {
    for (int j = 0; j < 3; j++) {
      xub[i][j] = x[i][j];
      xlb[i][j] = x[i][j];
      vub[i][j] = v[i][j];
      vlb[i][j] = v[i][j];
    }
  }

  // also sync other interval/ivector variables
  // comment when variables are not declared in this scope

  *interval_drag = interval(drag);
  *interval_t_start = interval(t_start);
  *interval_t_stop = interval(t_stop);
  *interval_t_current = interval(t_current);
  *interval_t_target = interval(t_target); 
  // *interval_t_period = interval(t_period); 
  *interval_t_freq = interval(t_freq);     
  *interval_pdrag_factor = interval(pdrag_factor);
  *interval_tdrag_factor = interval(tdrag_factor);
  // *interval_kecurrent = interval(kecurrent); 
  // *interval_expfac = interval(expfac);
  // *interval_factor_etap = interval(factor_etap);
  *interval_ke_target = interval(ke_target);
  *interval_factor_eta = interval(factor_eta);
  *interval_p_hydro = interval(p_hydro);
  // *interval_f_omega = interval(f_omega);
  *interval_mtk_term1 = interval(mtk_term1);
  *interval_mtk_term2 = interval(mtk_term2);

  for (int i = 0; i < 6; i++) ivector_p_start[i] = interval(p_start[i]); 
  for (int i = 0; i < 6; i++) ivector_p_stop[i] = interval(p_stop[i]);
  for (int i = 0; i < 6; i++) ivector_p_current[i] = interval(p_current[i]);
  for (int i = 0; i < 6; i++) ivector_p_target[i] = interval(p_target[i]); 
  // for (int i = 0; i < 6; i++) ivector_p_period[i] = interval(p_period[i]);
  for (int i = 0; i < 6; i++) ivector_p_freq[i] = interval(p_freq[i]);   
  for (int i = 0; i < mtchain; i++) ivector_eta[i] = interval(eta[i]);
  for (int i = 0; i < mtchain+1; i++) ivector_eta_dot[i] = interval(eta_dot[i]);
  for (int i = 0; i < mtchain; i++) ivector_eta_dotdot[i] = interval(eta_dotdot[i]);  
  for (int i = 0; i < mtchain; i++) ivector_eta_mass[i] = interval(eta_mass[i]); 
  for (int i = 0; i < 6; i++) ivector_omega[i] = interval(omega[i]); 
  for (int i = 0; i < 6; i++) ivector_omega_dot[i] = interval(omega_dot[i]); 
  for (int i = 0; i < 6; i++) ivector_omega_mass[i] = interval(omega_mass[i]); 
  for (int i = 0 ; i < mpchain; i++) ivector_etap[i] = interval(etap[i]);
  for (int i = 0; i < mpchain+1; i++) ivector_etap_dot[i] = interval(etap_dot[i]);
  for (int i = 0; i < mpchain; i++) ivector_etap_dotdot[i] = interval(etap_dotdot[i]); 
  for (int i = 0; i < mpchain; i++) ivector_etap_mass[i] = interval(etap_mass[i]);
  for (int i = 0; i < 6; i++) ivector_sigma[i] = interval(sigma[i]); 
  for (int i = 0; i < 6; i++) ivector_h[i] = interval(h[i]); 
  for (int i = 0; i < 6; i++) ivector_fdev[i] = interval(fdev[i]); 
  // for (int i = 0; i < 3; i++) ivector_factor[i] = interval(factor[i]);
  // for (int i = 0; i < 6; i++) ivector_tensor[i] = interval(tensor[i]);


}