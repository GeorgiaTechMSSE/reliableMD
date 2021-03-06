# reliableMD
A reliable molecular dynamics simulation with the consideration of model-form uncertainty in interatomic potential

There are 4 different sources of R-MD that comes along with the paper: 

Tran, Anh V., and Yan Wang. "Reliable Molecular Dynamics: Uncertainty quantification using interval analysis in molecular dynamics simulation." 
Computational Materials Science 127 (2017): 141-160.

To compile the source from a specific source folder, use 

make openmpi

to compile LAMMPS source code. 
To use a newer version, please study the difference between original/modified source code in the -28Jun14 before applying on the newer LAMMPS version.
While other compiling options are available, they have not been tested under the development process. There are several sources folders within this directory as follows. 
They are modified from the LAMMPS (lammps.sandia.gov) of the 28Jun14 version.

src-cxsc: interval statistical ensemble scheme

src-midpoint-radius: mid-point radius scheme 

src-original: original source lammps-28Jun14

src-total-uncertainty-abs: total uncertainty principle scheme with a fixed percentage value of absolute atoms' velocities

src-total-uncertainty-noabs: total uncertainty principle scheme with a fixed percentage value of atoms' velocities

src-ublb: lower-upper bounds scheme 

For the src-cxsc, one needs to install a modified version C-XSC v2.5.4 in the cxsc-2-5-4. 
The original website of C-XSC interval library is: http://www2.math.uni-wuppertal.de/~xsc/xsc/cxsc/apidoc/html/index.html. 
However, the source code has been modified to include Kaucher (generalized) intervals, where the lowerbound can be larger than the upperbound. 
To install the C-XSC, please follow the instruction of the corresponding website, as described above. 

The authors can be reached at anh.vt2@gatech.edu/anh.vt2@gmail.com for further question.
