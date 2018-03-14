#---------------------------------------------------------------------------------------------------
#  RootVariables.cmake
#---------------------------------------------------------------------------------------------------
set(rootpackagemap BASE ALL)
SET_PROPERTY(GLOBAL PROPERTY ALL "")
SET_PROPERTY(GLOBAL PROPERTY BASE "")
SET_PROPERTY(GLOBAL PROPERTY rootpackagemap "")

# Hard-coded version of ROOT map
# Multiproc depends on Net -> moving it to ALL

set(BASE interpreter core io rootpcm main clib clingutils cont dictgen foundation meta metacling
        multiproc rint textinput thread imt zip lzma lz4 newdelete unix winnt macosx base rootcling_stage1 src)

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
