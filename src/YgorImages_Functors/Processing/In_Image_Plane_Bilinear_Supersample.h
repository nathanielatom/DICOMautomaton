//In_Image_Plane_Bilinear_Supersample.h.
#pragma once


#include <cmath>
#include <any>
#include <functional>
#include <limits>
#include <list>
#include <map>

#include "YgorImages.h"
#include "YgorMath.h"
#include "YgorMisc.h"

template <class T> class contour_collection;

struct InImagePlaneBilinearSupersampleUserData {
    // These factors determine the number of rows and columns in the outgoing image. Must be positive integers.
    // There will be (RowScaleFactor) times as many rows outgoing as there are incoming, etc..
    long int RowScaleFactor    = 2;
    long int ColumnScaleFactor = 2;
};

bool InImagePlaneBilinearSupersample(planar_image_collection<float,double>::images_list_it_t first_img_it,
                                    std::list<planar_image_collection<float,double>::images_list_it_t> ,
                                    std::list<std::reference_wrapper<planar_image_collection<float,double>>>,
                                    std::list<std::reference_wrapper<contour_collection<double>>>, 
                                    std::any ud);

