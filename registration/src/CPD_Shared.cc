#include "YgorFilesDirs.h"    //Needed for Does_File_Exist_And_Can_Be_Read(...), etc..
#include "YgorMisc.h"         //Needed for FUNCINFO, FUNCWARN, FUNCERR macros.
#include "YgorMath.h"         //Needed for samples_1D.
#include "YgorString.h"       //Needed for GetFirstRegex(...)
#include "CPD_Shared.h"
#include <chrono>
using namespace std::chrono;

Eigen::MatrixXd CenterMatrix(const Eigen::MatrixXd & points,
            const Eigen::MatrixXd & meanVector) {
    Eigen::MatrixXd oneVec = Eigen::MatrixXd::Ones(points.rows(),1);
    return points - oneVec * meanVector.transpose();
}

Eigen::MatrixXd GetTranslationVector(const Eigen::MatrixXd & rotationMatrix,
            const Eigen::MatrixXd & xMeanVector,
            const Eigen::MatrixXd & yMeanVector,
            double scale) {
    return xMeanVector - scale * rotationMatrix * yMeanVector;
}

Eigen::MatrixXd AlignedPointSet(const Eigen::MatrixXd & yPoints,
            const Eigen::MatrixXd & rotationMatrix,
            const Eigen::MatrixXd & translation,
            double scale) {
    
    Eigen::MatrixXd oneVec = Eigen::MatrixXd::Ones(yPoints.rows(),1);
    return scale * yPoints * rotationMatrix.transpose() + oneVec * translation.transpose();
}

double Init_Sigma_Squared(const Eigen::MatrixXd & xPoints,
            const Eigen::MatrixXd & yPoints) {
    double normSum = 0;
    int nRowsX = xPoints.rows();
    int mRowsY =  yPoints.rows();
    int dim = xPoints.cols();
    for (int i = 0; i < nRowsX; i++) {
        const auto xRow = xPoints.row(i).transpose();
        for (int j = 0; j < mRowsY; j++) {
            const auto yRow = yPoints.row(j).transpose();
            auto rowDiff = xRow - yRow;
            // FUNCINFO(normSum)
            normSum += rowDiff.squaredNorm();
        }
    }
    return 1.0 / (nRowsX * mRowsY * dim) * normSum;
}

Eigen::MatrixXd E_Step(const Eigen::MatrixXd & xPoints,
            const Eigen::MatrixXd & yPoints,
            const Eigen::MatrixXd & rotationMatrix,
            const Eigen::MatrixXd & t,
            double sigmaSquared,
            double w,
            double scale) {
    Eigen::MatrixXd postProb = Eigen::MatrixXd::Zero(yPoints.rows(),xPoints.rows());
    Eigen::MatrixXd expMat = Eigen::MatrixXd::Zero(yPoints.rows(),xPoints.rows());

    Eigen::MatrixXd tempVector;
    double expArg;
    double numerator;
    double denominator;

    int mRowsY = yPoints.rows();
    int nRowsX = xPoints.rows();
    int dimensionality = yPoints.cols();

    // std::cout << "\n";
    // std::cout << "\n";    
    auto start = high_resolution_clock::now();
    for (size_t m = 0; m < mRowsY; ++m) {
        for (size_t n = 0; n < nRowsX; ++n) {
            tempVector = xPoints.row(n).transpose() - (rotationMatrix * yPoints.row(m).transpose() + t);
            expArg = - 1 / (2 * sigmaSquared) * tempVector.squaredNorm();
            expMat(m,n) = exp(expArg);
        }
    }

    auto stop = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(stop - start); 
    // std::cout << duration.count() << std::endl; 

    for (size_t m = 0; m < mRowsY; ++m) {
        for (size_t n = 0; n < nRowsX; ++n) {
            numerator = expMat(m,n);
            denominator = expMat.col(n).sum() + 
                          pow(2 * M_PI * sigmaSquared,((double)(dimensionality/2))) * (w/(1-w)) * (double)(mRowsY / nRowsX);
            postProb(m,n) = numerator / denominator;
        }
    }
    stop = high_resolution_clock::now();
    duration = duration_cast<microseconds>(stop - start); 
    // std::cout << duration.count() << std::endl; 
    // std::cout << "\n";
    // std::cout << "\n";
    
    return postProb;
}

Eigen::MatrixXd CalculateUx(
            const Eigen::MatrixXd & xPoints, 
            const Eigen::MatrixXd & postProb){
    Eigen::MatrixXd oneVec = Eigen::MatrixXd::Ones(postProb.rows(),1);
    double oneOverNp = 1/(postProb.sum());
    return oneOverNp * (xPoints.transpose()) * postProb.transpose() * oneVec;
}

Eigen::MatrixXd CalculateUy( 
            const Eigen::MatrixXd & yPoints, 
            const Eigen::MatrixXd & postProb){
    Eigen::MatrixXd oneVec = Eigen::MatrixXd::Ones(postProb.cols(),1);
    double oneOverNp = 1/(postProb.sum());
    return oneOverNp * (yPoints.transpose()) * postProb * oneVec;
}