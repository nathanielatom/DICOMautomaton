//Run_Def_Reg.cc - A part of DICOMautomaton 2021. Written by hal clark, ...
//
// This program will load files, parse arguments, and run a registration model.

#include <exception>
#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <string>    
#include <vector>

#include <boost/filesystem.hpp>
#include <cstdlib>            //Needed for exit() calls.
#include <utility>            //Needed for std::pair.
#include <chrono>

#include "YgorArguments.h"    //Needed for ArgumentHandler class.
#include "YgorFilesDirs.h"    //Needed for Does_File_Exist_And_Can_Be_Read(...), etc..
#include "YgorMisc.h"         //Needed for FUNCINFO, FUNCWARN, FUNCERR macros.
#include "YgorMath.h"         //Needed for various geometry classes (e.g., vec3).
#include "YgorImages.h"       //Needed for planar_image_collection and planar_image classes.
#include "YgorImagesIOFITS.h" //Needed for reading and writing images in FITS format, which preserves embedded metadata.
#include "YgorString.h"       //Needed for GetFirstRegex(...)

using namespace std::chrono;

int main(int argc, char* argv[]){
    //This is the main entry-point for an experimental implementation of the ABC deformable registration algorithm. This
    //function is called when the program is executed. The interface to this program should be kept relatively simple to
    //simplify later integration into the broader DICOMautomaton code base.


    // The 'moving' image array. This is the image array that will be transformed to match the stationary
    // image array.
    planar_image_collection<float, double> moving;

    // The 'stationary' image array. This set of images will be considered the reference or target image array.
    // The deformable registration algorithm will attempt to create a transformation that maps the moving set to
    // the stationary set.
    planar_image_collection<float, double> stationary;

    // See below for description of these parameters.
    std::string type = "rigid";
    long int iters = 1;

    // This structure is described in Alignment_ABC.h.
    CPDParams params;
    
    //================================================ Argument Parsing ==============================================

    class ArgumentHandler arger;
    const std::string progname(argv[0]);
    arger.examples = { { "--help", 
                         "Show the help screen and some info about the program." },
                       { "-m moving.fits -s stationary.fits",
                         "Load a moving image array, a stationary image array, and run the"
                         " deformable registration algorithm." }
                     };
    arger.description = "A program for running a deformable registration algorithm.";

    arger.default_callback = [](int, const std::string &optarg) -> void {
      FUNCERR("Unrecognized option with argument: '" << optarg << "'");
      return; 
    };
    arger.optionless_callback = [&](const std::string &optarg) -> void {
      FUNCERR("Unrecognized option with argument: '" << optarg << "'");
      return; 
    };
    arger.push_back( ygor_arg_handlr_t(1, 'm', "moving", true, "moving.fits",
      "Load a moving image array from the given file.",
      [&](const std::string &optarg) -> void {
        if(!moving.images.emplace_back(optarg)){
          FUNCERR("Unable to parse moving image array file: '" << optarg << "'");
          exit(1);
        }
        return;
      })
    );
    arger.push_back( ygor_arg_handlr_t(1, 's', "stationary", true, "stationary.fits",
      "Load a stationary image array from the given file.",
      [&](const std::string &optarg) -> void {
        if(!stationary.images.emplace_back(optarg)){
          FUNCERR("Unable to parse stationary image array file: '" << optarg << "'");
          exit(1);
        }
        return;
      })
    );

    arger.push_back( ygor_arg_handlr_t(1, 't', "type", true, "rigid",
      "Which algorithm to use. Options: rigid, ...",
      [&](const std::string &optarg) -> void {
        type = optarg;
        return;
      })
    );
    arger.push_back( ygor_arg_handlr_t(1, 'd', "iterations", true, "1",
      "Number of iterations to perform.",
      [&](const std::string &optarg) -> void {
        iters = std::stol(optarg);
        return;
      })
    );
    arger.push_back( ygor_arg_handlr_t(1, 't', "tune", true, "1.23",
      "Numerical factor that can tune the algorithm.",
      [&](const std::string &optarg) -> void {
        tune = std::stod(optarg);
        return;
      })
    );
    arger.Launch(argc, argv);

    //============================================= Input Validation ================================================
    if(moving.images.empty()){
        FUNCERR("Moving image array contains no images. Unable to continue.");
    }
    if(stationary.images.empty()){
        FUNCERR("Stationary image array contains no images. Unable to continue.");
    }

    //============================================ Perform Registration  =============================================

    high_resolution_clock::time_point start = high_resolution_clock::now();
    if(type == "rigid") {
        
        // Perform your registration algorithm here.
        // The result is a transform that can be saved, applied to the moving images, or applied to other kinds of
        // objects (e.g., surfac meshes).
        RigidCPDTransform transform = AlignViaRigidCPD(params, moving, stationary, iter_interval, video, xyz_outfile);

        // If needed (for testing, debugging, ...) try to apply the transform.
        //transform.apply_to(moving);

        // If needed, try save the transform by writing it to file.
        //transform.write_to("transform.txt");
    } else {
        FUNCERR("Specified algorithm specified was invalid. Options are rigid, ...");
        return 1;
    }

    high_resolution_clock::time_point stop = high_resolution_clock::now();
    duration<double> time_span = duration_cast<duration<double>>(stop - start);
    FUNCINFO("Excecution took time: " << time_span.count())
    return 0;
}