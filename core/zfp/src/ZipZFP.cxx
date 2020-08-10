// Author:

/*************************************************************************
 * Copyright (C) 1995-2020, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#include "ZipZFP.h"

#include "ROOT/RConfig.hxx"

#include <zfp.h>

void R__zipZFP(){

	zfp_type type = zfp_type_double;
	zfp_stream* zfp = zfp_stream_open(NULL);

}
