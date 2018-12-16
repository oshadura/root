#---------------------------------------------------------------------------------------------------
#  RootPackageMap.cmake
#---------------------------------------------------------------------------------------------------
# A set of requested packages for ROOT (dynamic map)
set(rootpackagemap_requested BASE)
SET_PROPERTY(GLOBAL PROPERTY rootpackagemap_requested "")

# Root Map File
set(rootpackagemap BASE ALL IO)
SET_PROPERTY(GLOBAL PROPERTY rootpackagemap "")

# Hard-coded version of ROOT map should be:
# set(rootpackagemap BASE ALL X X1 X2 X3 IO)
# FIXME: now we going to test only harcoded and simplified version
# to make it more dynamic we will need to add function ADD_ROOT_PACKAGE_TO_MAP(RootModularization.cmake)
# with rootpackagemap dynamically extended

# We consider a name of module is a directory name
# Multiproc from Core depends on Net -> moving it to ALL
set(BASE interpreter llvm/src cling core io rootpcm main clib clingutils cont dictgen foundation meta metacling
         rint textinput thread imt zip lzma lz4 newdelete unix winnt macosx base rootcling_stage1 src)
SET_PROPERTY(GLOBAL PROPERTY BASE "")

set(ALL multiproc pyroot ruby r tmva tmvagui pymva rmva hist histdraw xml
        histpainter spectrum spectrumpainter unfold hbook build math hist tree
        net graf2d graf3d gui proof html montecarlo geom rootx misc bindings
        tmva roofit primitives gpadv7 gpad graf postscript mathtext tmva roofit primitives gpadv7
        gpad graf postscript mathtext win32gdk x11 x11ttf asimage qt gviz fitsio quartz
        cocoa net auth bonjour krb5auth rpdutils rootd netx alien monalisa ldap globusauth
        davix netxng http httpsniff minicern memstat table mysql oracle odbc pgsql sqlite
        g3d x3d eve gl glew ftgl gviz3d geom geombuilder gdml geocad vecgeom mathcore mathmore
        matrix minuit minuit2 fumili physics mlp quadp foam smatrix splot genvector genetic
        unuran fftw rtools roofitcore roofit roostats histfactory sql eg vmc pythia6 pythia8
        xmlparser castor rfio gfal dcache hdfs tree treeplayer treeviewer dataframe gui ged fitpanel
        guibuilder guihtml recorder sessionviewer
        webdisplay cefdisplay qt5webdisplay canvaspainter fitpanelv7 qtgsi qtroot proof
        proofplayer proofbench afdsmgrd proofd proofx pq2)
SET_PROPERTY(GLOBAL PROPERTY ALL "")

# Skipping io/doc, io/rootpcm (Core) and io/io (Core)
set(IO castor chirp gfal hdfs rfio sql xml xmlparser)
SET_PROPERTY(GLOBAL PROPERTY IO "")

###################################### TBD ########################################################
# Dynamic function that will add new packages/modules in rootpackagemap
function(SET_ROOT_PACKAGE X values_list)
 set(X {values_list})
 SET_PROPERTY(GLOBAL PROPERTY X "")
endfunction()

function(ADD_ROOT_PACKAGE_TO_MAP X)
   set(rootpackagemap_requested ${rootpackagemap_requested} ${X})
endfunction()
