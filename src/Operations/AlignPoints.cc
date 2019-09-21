//AlignPoints.cc - A part of DICOMautomaton 2019. Written by hal clark.

#include <asio.hpp>
#include <algorithm>
#include <experimental/optional>
#include <fstream>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <set> 
#include <stdexcept>
#include <string>    
#include <utility>            //Needed for std::pair.
#include <vector>

#include <eigen3/Eigen/Dense>
#include <eigen3/Eigen/Eigenvalues>
#include <eigen3/Eigen/SVD>

#include "../Structs.h"
#include "../Regex_Selectors.h"
#include "../Thread_Pool.h"
#include "AlignPoints.h"
#include "Explicator.h"       //Needed for Explicator class.
#include "YgorImages.h"
#include "YgorMath.h"         //Needed for vec3 class.
#include "YgorMisc.h"         //Needed for FUNCINFO, FUNCWARN, FUNCERR macros.
#include "YgorStats.h"        //Needed for Stats:: namespace.
#include "YgorString.h"       //Needed for GetFirstRegex(...)
#include "../Surface_Meshes.h"


struct
AffineTransform {

    private:
        // The top-left 3x3 sub-matrix is a rotation matrix. The top right-most column 3-vector is a translation vector.
        //
        //     (0,0)    (1,0)    (2,0)  |  (3,0)                               |                  
        //     (0,1)    (1,1)    (2,1)  |  (3,1)             linear transform  |  translation     
        //     (0,2)    (1,2)    (2,2)  |  (3,2)     =                         |                  
        //     ---------------------------------           ------------------------------------   
        //     (0,3)    (1,3)    (2,3)  |  (3,3)                 (zeros)       |    scale        
        //
        // Note that the bottom row must remain unaltered to be an Affine transform.
        std::array< std::array<double, 4>, 4> t = {{ std::array<double,4>{{ 1.0, 0.0, 0.0, 0.0 }},
                                                     std::array<double,4>{{ 0.0, 1.0, 0.0, 0.0 }},
                                                     std::array<double,4>{{ 0.0, 0.0, 1.0, 0.0 }},
                                                     std::array<double,4>{{ 0.0, 0.0, 0.0, 1.0 }} }};

    public:
        // Accessors.
        double &
        coeff(long int i, long int j){
            if(!isininc(0L,i,3L) || !isininc(0L,j,2L)) throw std::invalid_argument("Tried to access fixed coefficients."
                                                                                   " Refusing to continue.");
            return this->t[i][j];
        }

        // Apply the (full) transformation to a vec3.
        vec3<double> 
        apply_to(const vec3<double> &in) const {
            const auto x = (in.x * this->t[0][0]) + (in.y * this->t[1][0]) + (in.z * this->t[2][0]) + (1.0 * this->t[3][0]);
            const auto y = (in.x * this->t[0][1]) + (in.y * this->t[1][1]) + (in.z * this->t[2][1]) + (1.0 * this->t[3][1]);
            const auto z = (in.x * this->t[0][2]) + (in.y * this->t[1][2]) + (in.z * this->t[2][2]) + (1.0 * this->t[3][2]);
            const auto w = (in.x * this->t[0][3]) + (in.y * this->t[1][3]) + (in.z * this->t[2][3]) + (1.0 * this->t[3][3]);

            if(w != 1.0) throw std::runtime_error("Transformation is not Affine. Refusing to continue.");

            return vec3<double>(x, y, z);
        }

        // Apply the transformation to a point cloud.
        void
        apply_to(Point_Cloud &in){
            for(auto &p : in.points){
                p.first = this->apply_to(p.first);
            }
            return;
        }
};


// This routine performs a simple COM-based alignment.
//
// The resultant transformation is a rotation-less shift so the point cloud centres-of-mass overlap.
//
// Note that this routine only identifies a transform, it does not implement it by altering the point clouds.
//
static
std::experimental::optional<AffineTransform>
AlignViaCOM(std::reference_wrapper<Point_Cloud> moving,
            std::reference_wrapper<Point_Cloud> stationary){
    AffineTransform t;

    // Compute the COM for both point clouds.
    const auto est_COM = [](const Point_Cloud &pc) -> vec3<double> {
        const auto N = pc.points.size();
        Stats::Running_Sum<double> COM_sx;
        Stats::Running_Sum<double> COM_sy;
        Stats::Running_Sum<double> COM_sz;
        for(const auto &p : pc.points){
            COM_sx.Digest(p.first.x);
            COM_sy.Digest(p.first.y);
            COM_sz.Digest(p.first.z);
        }
        return vec3<double>(COM_sx.Current_Sum(),
                            COM_sy.Current_Sum(),
                            COM_sz.Current_Sum()) / static_cast<double>(N);
    };
    const auto COM_s = est_COM(stationary.get());
    const auto COM_m = est_COM(moving.get());

    const auto dCOM = (COM_s - COM_m);
    t.coeff(3,0) = dCOM.x;
    t.coeff(3,1) = dCOM.y;
    t.coeff(3,2) = dCOM.z;

    return t;
}
    
// This routine performs a PCA-based alignment.
//
// First, the moving point cloud is translated the moving point cloud so the centre of mass aligns to the reference
// point cloud, performs PCA separately on the reference and moving point clouds, compute distribution moments along
// each axis to determine the direction, and then rotates the moving point cloud so the principle axes coincide.
//
// Note that this routine only identifies a transform, it does not implement it by altering the point clouds.
//
static
std::experimental::optional<AffineTransform>
AlignViaPCA(std::reference_wrapper<Point_Cloud> moving,
            std::reference_wrapper<Point_Cloud> stationary){
    AffineTransform t;

    // Compute the COM for both point clouds.
    const auto est_COM = [](const Point_Cloud &pc) -> vec3<double> {
        const auto N = pc.points.size();
        Stats::Running_Sum<double> COM_sx;
        Stats::Running_Sum<double> COM_sy;
        Stats::Running_Sum<double> COM_sz;
        for(const auto &p : pc.points){
            COM_sx.Digest(p.first.x);
            COM_sy.Digest(p.first.y);
            COM_sz.Digest(p.first.z);
        }
        return vec3<double>(COM_sx.Current_Sum(),
                            COM_sy.Current_Sum(),
                            COM_sz.Current_Sum()) / static_cast<double>(N);
    };
    const auto COM_s = est_COM(stationary.get());
    const auto COM_m = est_COM(moving.get());
    
    // Compute the PCA for both point clouds.
    struct pcomps {
        vec3<double> pc1;
        vec3<double> pc2;
        vec3<double> pc3;
    };
    const auto est_PCA = [](const Point_Cloud &pc) -> pcomps {
        // Determine the three most prominent unit vectors via PCA.
        Eigen::MatrixXd mat;
        const size_t mat_rows = pc.points.size();
        const size_t mat_cols = 3;
        mat.resize(mat_rows, mat_cols);
        {
            size_t i = 0;
            for(const auto &v : pc.points){
                mat(i, 0) = static_cast<double>(v.first.x);
                mat(i, 1) = static_cast<double>(v.first.y);
                mat(i, 2) = static_cast<double>(v.first.z);
                ++i;
            }
        }

        Eigen::MatrixXd centered = mat.rowwise() - mat.colwise().mean();
        Eigen::MatrixXd cov = centered.adjoint() * centered;
        Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> eig(cov);
        
        Eigen::VectorXd evals = eig.eigenvalues();
        Eigen::MatrixXd evecs = eig.eigenvectors().real();

        pcomps out;
        out.pc1 = vec3<double>( evecs(0,0), evecs(1,0), evecs(2,0) ).unit();
        out.pc2 = vec3<double>( evecs(0,1), evecs(1,1), evecs(2,1) ).unit();
        out.pc3 = vec3<double>( evecs(0,2), evecs(1,2), evecs(2,2) ).unit();
        return out;
    };

    const auto pcomps_stationary = est_PCA(stationary);
    const auto pcomps_moving = est_PCA(moving);

    // Compute COM-centered third-order moments (i.e., skew) along each component and use them to reorient the principle components.
    // The third order is needed since the first-order (mean) is eliminated via COM-shifting, and the second order
    // (variance) cannot differentiate positive and negative directions.
    const auto reorient_pcomps = [](const vec3<double> &COM,
                                    const pcomps &comps,
                                    const Point_Cloud &pc) {

        Stats::Running_Sum<double> rs_pc1;
        Stats::Running_Sum<double> rs_pc2;
        Stats::Running_Sum<double> rs_pc3;
        for(const auto &v : pc.points){
            const auto sv = (v.first - COM);

            const auto proj_pc1 = sv.Dot(comps.pc1);
            rs_pc1.Digest( std::pow(proj_pc1, 3.0) );
            const auto proj_pc2 = sv.Dot(comps.pc2);
            rs_pc2.Digest( std::pow(proj_pc2, 3.0) );
            const auto proj_pc3 = sv.Dot(comps.pc3);
            rs_pc3.Digest( std::pow(proj_pc3, 3.0) );
        }

        pcomps out;
        out.pc1 = (comps.pc1 * rs_pc1.Current_Sum()).unit(); // Will be either + or - the original pcomps.
        out.pc2 = (comps.pc2 * rs_pc2.Current_Sum()).unit(); // Will be either + or - the original pcomps.
        out.pc3 = (comps.pc3 * rs_pc3.Current_Sum()).unit(); // Will be either + or - the original pcomps.
        return out;
    };

    const auto reoriented_pcomps_stationary = reorient_pcomps(COM_s,
                                                              pcomps_stationary,
                                                              stationary);
    const auto reoriented_pcomps_moving = reorient_pcomps(COM_m,
                                                          pcomps_moving,
                                                          moving);

    FUNCINFO("Stationary point cloud:");
    FUNCINFO("    COM                  : " << COM_s);
    FUNCINFO("    pcomp_pc1            : " << pcomps_stationary.pc1);
    FUNCINFO("    pcomp_pc2            : " << pcomps_stationary.pc2);
    FUNCINFO("    pcomp_pc3            : " << pcomps_stationary.pc3);
    FUNCINFO("    reoriented_pcomp_pc1 : " << reoriented_pcomps_stationary.pc1);
    FUNCINFO("    reoriented_pcomp_pc2 : " << reoriented_pcomps_stationary.pc2);
    FUNCINFO("    reoriented_pcomp_pc3 : " << reoriented_pcomps_stationary.pc3);

    FUNCINFO("Moving point cloud:");
    FUNCINFO("    COM                  : " << COM_m);
    FUNCINFO("    pcomp_pc1            : " << pcomps_moving.pc1);
    FUNCINFO("    pcomp_pc2            : " << pcomps_moving.pc2);
    FUNCINFO("    pcomp_pc3            : " << pcomps_moving.pc3);
    FUNCINFO("    reoriented_pcomp_pc1 : " << reoriented_pcomps_moving.pc1);
    FUNCINFO("    reoriented_pcomp_pc2 : " << reoriented_pcomps_moving.pc2);
    FUNCINFO("    reoriented_pcomp_pc3 : " << reoriented_pcomps_moving.pc3);

    // Determine the linear transformation that will align the reoriented principle components.
    //
    // If we assemble the orthonormal principle component vectors for each cloud into a 3x3 matrix (i.e., three column
    // vectors) we get an orthonormal matrix. The transformation matrix 'A' needed to transform the moving matrix 'M'
    // into the stationary matrix 'S' can be found from $S = AM$. Since M is orthonormal, $M^{-1}$ always exists and
    // also $M^{-1} = M^{T}$. So $A = SM^{T}$.

    {
        Eigen::Matrix3d S;
        S(0,0) = reoriented_pcomps_stationary.pc1.x;
        S(1,0) = reoriented_pcomps_stationary.pc1.y;
        S(2,0) = reoriented_pcomps_stationary.pc1.z;

        S(0,1) = reoriented_pcomps_stationary.pc2.x;
        S(1,1) = reoriented_pcomps_stationary.pc2.y;
        S(2,1) = reoriented_pcomps_stationary.pc2.z;

        S(0,2) = reoriented_pcomps_stationary.pc3.x;
        S(1,2) = reoriented_pcomps_stationary.pc3.y;
        S(2,2) = reoriented_pcomps_stationary.pc3.z;

        Eigen::Matrix3d M;
        M(0,0) = reoriented_pcomps_moving.pc1.x;
        M(1,0) = reoriented_pcomps_moving.pc1.y;
        M(2,0) = reoriented_pcomps_moving.pc1.z;

        M(0,1) = reoriented_pcomps_moving.pc2.x;
        M(1,1) = reoriented_pcomps_moving.pc2.y;
        M(2,1) = reoriented_pcomps_moving.pc2.z;

        M(0,2) = reoriented_pcomps_moving.pc3.x;
        M(1,2) = reoriented_pcomps_moving.pc3.y;
        M(2,2) = reoriented_pcomps_moving.pc3.z;

        auto A = S * M.transpose();
        // Force the transform to be the identity for debugging.
        //A << 1.0, 0.0, 0.0,
        //     0.0, 1.0, 0.0, 
        //     0.0, 0.0, 1.0;

        t.coeff(0,0) = A(0,0);
        t.coeff(0,1) = A(1,0);
        t.coeff(0,2) = A(2,0);

        t.coeff(1,0) = A(0,1);
        t.coeff(1,1) = A(1,1);
        t.coeff(1,2) = A(2,1);

        t.coeff(2,0) = A(0,2);
        t.coeff(2,1) = A(1,2);
        t.coeff(2,2) = A(2,2);

        // Work out the translation vector.
        //
        // Because the COM is not explicitly subtracted, we have to incorporate the subtraction into the translation term.
        // Ideally we would perform $A * (M - COM_{M}) + COM_{S}$ explicitly; to emulate this, we can rearrange to find
        // $A * M + \left( COM_{S} - A * COM_{M} \right) \equiv A * M + b$ where $b = COM_{S} - A * COM_{M}$ is the
        // necessary translation term.
        {
            Eigen::Vector3d e_COM_m(COM_m.x, COM_m.y, COM_m.z);
            auto A_e_COM_m = A * e_COM_m; 

            t.coeff(3,0) = COM_s.x - A_e_COM_m(0);
            t.coeff(3,1) = COM_s.y - A_e_COM_m(1);
            t.coeff(3,2) = COM_s.z - A_e_COM_m(2);
        }
    }

    FUNCINFO("Final linear transform:");
    FUNCINFO("    ( " << t.coeff(0,0) << "  " << t.coeff(1,0) << "  " << t.coeff(2,0) << " )");
    FUNCINFO("    ( " << t.coeff(0,1) << "  " << t.coeff(1,1) << "  " << t.coeff(2,1) << " )");
    FUNCINFO("    ( " << t.coeff(0,2) << "  " << t.coeff(1,2) << "  " << t.coeff(2,2) << " )");
    FUNCINFO("Final translation:");
    FUNCINFO("    ( " << t.coeff(3,0) << " )");
    FUNCINFO("    ( " << t.coeff(3,1) << " )");
    FUNCINFO("    ( " << t.coeff(3,2) << " )");

    return t;
}




OperationDoc OpArgDocAlignPoints(void){
    OperationDoc out;
    out.name = "AlignPoints";

    out.desc = 
        "This operation aligns (i.e., 'registers') a 'moving' point cloud to a 'stationary' (i.e., 'reference') point cloud.";
        
    //out.notes.emplace_back(
    //    "Existing point clouds are ignored and unaltered."
    //);
        

    out.args.emplace_back();
    out.args.back() = PCWhitelistOpArgDoc();
    out.args.back().name = "MovingPointSelection";
    out.args.back().default_val = "last";
    out.args.back().desc = "The point cloud that will be transformed. "_s
                         + out.args.back().desc;


    out.args.emplace_back();
    out.args.back() = PCWhitelistOpArgDoc();
    out.args.back().name = "ReferencePointSelection";
    out.args.back().default_val = "last";
    out.args.back().desc = "The stationary point cloud to use as a reference for the moving point cloud. "_s
                         + out.args.back().desc
                         + " Note that this point cloud is not modified.";


    out.args.emplace_back();
    out.args.back().name = "Method";
    out.args.back().desc = "The alignment algorithm to use."
                           " Two rigid alignment options are available: 'COM' and 'PCA'."
                           ""
                           " The 'COM' option finds a rotationless translation the aligns the centre-of-mass (COM)"
                           " of the moving point cloud with that of the stationary point cloud."
                           " It is susceptible to noise and outliers, and can only be reliably used when the point"
                           " cloud has complete rotational symmetry (i.e., a sphere). On the other hand, 'COM'"
                           " alignment should never fail, and can handle a large number of points."
                           " COM alignment is frequently used as a pre-processing step for more advanced algorithms."
                           ""
                           " The 'PCA' option finds an Affine transformation by performing COM alignment,"
                           " performing principle component analysis (PCA) separately on the reference and moving"
                           " point clouds, computing third-order point distribution moments along each principle axis"
                           " to establish a consistent orientation,"
                           " and then rotates the moving point cloud so the principle axes of the stationary and"
                           " moving point clouds coincide."
                           " The 'PCA' method may be suitable when: (1) both clouds are not contaminated with extra"
                           " noise points (but some Gaussian noise in the form of point 'jitter' should be tolerated)"
                           " and (2) the clouds are not perfectly spherical (i.e., so they have valid principle"
                           " components)."
                           " However, note that the 'PCA' method is susceptible to outliers and can not scale"
                           " a point cloud.";
    out.args.back().default_val = "com";
    out.args.back().expected = true;
    out.args.back().examples = { "com", "pca" };


    return out;
}



Drover AlignPoints(Drover DICOM_data, OperationArgPkg OptArgs, std::map<std::string,std::string> /*InvocationMetadata*/, std::string FilenameLex){

    Explicator X(FilenameLex);

    //---------------------------------------------- User Parameters --------------------------------------------------
    const auto MovingPointSelectionStr = OptArgs.getValueStr("MovingPointSelection").value();
    const auto ReferencePointSelectionStr = OptArgs.getValueStr("ReferencePointSelection").value();

    const auto MethodStr = OptArgs.getValueStr("Method").value();

    //-----------------------------------------------------------------------------------------------------------------
    const auto regex_com = Compile_Regex("^co?m?$");
    const auto regex_pca = Compile_Regex("^pc?a?$");

    auto PCs_all = All_PCs( DICOM_data );
    auto ref_PCs = Whitelist( PCs_all, ReferencePointSelectionStr );
    if(ref_PCs.size() != 1){
        throw std::invalid_argument("A single reference point cloud must be selected. Cannot continue.");
    }

    // Iterate over the moving point clouds, aligning each to the reference point cloud.
    auto moving_PCs = Whitelist( PCs_all, MovingPointSelectionStr );
    for(auto & pcp_it : moving_PCs){
        FUNCINFO("There are " << (*pcp_it)->points.size() << " points in the moving point cloud");

        if(false){
        }else if( std::regex_match(MethodStr, regex_com) ){
            auto t_opt = AlignViaCOM( (*(*pcp_it)),
                                      (*(*ref_PCs.front())) );
 
            if(t_opt){
                FUNCINFO("Transforming the point cloud using centre-of-mass alignment");
                t_opt.value().apply_to(*(*pcp_it));
            }
        }else if( std::regex_match(MethodStr, regex_pca) ){
            auto t_opt = AlignViaPCA( (*(*pcp_it)),
                                      (*(*ref_PCs.front())) );
 
            if(t_opt){
                FUNCINFO("Transforming the point cloud using principle component alignment");
                t_opt.value().apply_to(*(*pcp_it));
            }
        }else{
            throw std::invalid_argument("Method not understood. Cannot continue.");
        }

    } // Loop over point clouds.

    return DICOM_data;
}
