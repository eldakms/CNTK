//
// <copyright file="CPUMatrix.h" company="Microsoft">
//     Copyright (c) Microsoft Corporation.  All rights reserved.
// </copyright>
//
#pragma once
#include <vector>
#include <stdio.h>
#include <ctime>
#include <limits.h>			/* LINUX */
#include "File.h"
#include "Helpers.h"
#include "CommonMatrix.h"

#ifndef	LINUX
#ifdef MATH_EXPORTS
#define MATH_API __declspec(dllexport)
#else
#define MATH_API __declspec(dllimport)
#endif

#else	/* LINUX */
#define	MATH_API 
#endif	/* LINUX */

#ifndef USE_TIME_BASED_SEED
#define USE_TIME_BASED_SEED ULONG_MAX
#endif
// NOTE NOTE NOTE:
// use CPUSingleMatrix and CPUDoubleMatrix instead of using the template directly
///////////////////////////////////////////////


// This class is exported from the Math.dll
namespace Microsoft { namespace MSR { namespace CNTK {

    #define LZERO  -10e10
    #define MINLOGEXP -9.2103
    #define LSMALL -0.5E10
    double logadd(double x, double y);

    //To compy with BLAS libraries matrices are stored in ColMajor. However, by default C/C++/C# use RowMajor
    //convertion is need when passing data between CPUMatrix and C++ matrices
    template<class ElemType>
    class MATH_API CPUMatrix : public BaseMatrix<ElemType>
    {
    public:
        CPUMatrix();
        CPUMatrix(FILE* f, const char * matrixName); //matrixName is used to verify that correct matrix is read.
        CPUMatrix(const size_t numRows, const size_t numCols);
        CPUMatrix(const size_t numRows, const size_t numCols, ElemType *pArray, const size_t matrixFlags=matrixFlagNormal);
        CPUMatrix(const CPUMatrix<ElemType>& deepCopyFrom);  //copy constructor, deep copy
        CPUMatrix<ElemType>& operator=(const CPUMatrix<ElemType>& deepCopyFrom);  //assignment operator, deep copy
        CPUMatrix(CPUMatrix<ElemType>&& moveFrom);  //move constructor, shallow copy
        CPUMatrix<ElemType>& operator=(CPUMatrix<ElemType>&& moveFrom);  //move assignment operator, shallow copy

        ~CPUMatrix();

    public:
        size_t BufferSize() const {return this->m_numRows*this->m_numCols*sizeof(ElemType);}
        ElemType* BufferPointer() const {return this->m_pArray;}

        CPUMatrix<ElemType> ColumnSlice(size_t startColumn, size_t numCols) const;
        CPUMatrix<ElemType>& AssignColumnSlice(const CPUMatrix<ElemType>& fromMatrix, size_t startColumn, size_t numCols);
        CPUMatrix<ElemType>& AssignColumnSlice(const ElemType val, size_t startColumn, size_t numCols);

        void Adagrad(CPUMatrix<ElemType>& gradients);
        void RmsProp(CPUMatrix<ElemType>& gradients);

        void Reshape(const size_t numRows, const size_t numCols);
        void Resize(const size_t numRows, const size_t numCols, bool growOnly = true);  //by default we only reallocate if need to grow
        ElemType* CopyToArray() const; //allocated by the callee but need to be deleted by the caller
        size_t CopyToArray(ElemType*& arrayCopyTo, size_t& currentArraySize) const;  //allocated by the callee but need to be deleted by the caller

        inline ElemType& operator() (const size_t row, const size_t col) 
        {
            return this->m_pArray[LocateElement(row, col)];
        }
        inline const ElemType& operator() (const size_t row, const size_t col) const 
        {
            return this->m_pArray[LocateElement(row, col)];
        }
        inline ElemType Get00Element() const 
        {
            return this->m_pArray[0];
        }        

        void SetValue(const ElemType v);
        void SetValue(const CPUMatrix<ElemType>& deepCopyFrom);
        void SetValue(const size_t numRows, const size_t numCols, ElemType *pArray, size_t matrixFlags=matrixFlagNormal);
        void SetColumn(const ElemType* colPointer, size_t colInd);
        void SetColumn(const CPUMatrix<ElemType>& valMat, size_t colInd);
        void SetColumn(const ElemType val, size_t j);

        void SetDiagonalValue(const ElemType v);
        void SetDiagonalValue(CPUMatrix<ElemType>& vector);
        void SetUniformRandomValue(const ElemType low, const ElemType high, unsigned long seed=USE_TIME_BASED_SEED); 
        void SetGaussianRandomValue(const ElemType mean, const ElemType sigma, unsigned long seed=USE_TIME_BASED_SEED);
        void SetUniformRandomMask(const ElemType maskRate, const ElemType scaleValue, unsigned long seed=USE_TIME_BASED_SEED); 
        void AddGaussianRandomValue(const ElemType mean, const ElemType sigma, unsigned long seed=USE_TIME_BASED_SEED);

        CPUMatrix<ElemType> Transpose();
        CPUMatrix<ElemType>& AssignTransposeOf (const CPUMatrix<ElemType>& a);

        CPUMatrix<ElemType>& operator+= (const ElemType alpha);
        CPUMatrix<ElemType> operator+ (const ElemType alpha) const;
        CPUMatrix<ElemType>& AssignSumOf(const ElemType alpha, const CPUMatrix<ElemType>& a);

        CPUMatrix<ElemType>& operator+= (const CPUMatrix<ElemType>& a);
        CPUMatrix<ElemType> operator+ (const CPUMatrix<ElemType>& a) const;
        CPUMatrix<ElemType>& AssignSumOf(const CPUMatrix<ElemType>& a, const CPUMatrix<ElemType>& b);

        CPUMatrix<ElemType>& operator-= (const ElemType alpha);
        CPUMatrix<ElemType> operator- (const ElemType alpha) const;
        CPUMatrix<ElemType>& AssignDifferenceOf(const ElemType alpha, const CPUMatrix<ElemType>& a);
        CPUMatrix<ElemType>& AssignDifferenceOf(const CPUMatrix<ElemType>& a, const ElemType alpha);

        CPUMatrix<ElemType>& operator-= (const CPUMatrix<ElemType>& a);
        CPUMatrix<ElemType> operator- (const CPUMatrix<ElemType>& a) const;
        CPUMatrix<ElemType>& AssignDifferenceOf(const CPUMatrix<ElemType>& a, const CPUMatrix<ElemType>& b);

        CPUMatrix<ElemType>& operator*= (const ElemType alpha);
        CPUMatrix<ElemType> operator* (const ElemType alpha) const;
        CPUMatrix<ElemType>& AssignProductOf(const ElemType alpha, const CPUMatrix<ElemType>& a);

        CPUMatrix<ElemType> operator* (const CPUMatrix<ElemType>& a) const;
        CPUMatrix<ElemType>& AssignProductOf (const CPUMatrix<ElemType>& a, const bool transposeA, const CPUMatrix<ElemType>& b, const bool transposeB);

        CPUMatrix<ElemType>& operator/= (ElemType alpha);
        CPUMatrix<ElemType> operator/ (ElemType alpha) const;

        CPUMatrix<ElemType>& operator^= (ElemType alpha); //element-wise power
        CPUMatrix<ElemType> operator^ (ElemType alpha) const; //element-wise power
        CPUMatrix<ElemType>& AssignElementPowerOf(const CPUMatrix<ElemType>& a, const ElemType power);

        CPUMatrix<ElemType>& ElementMultiplyWith (const CPUMatrix<ElemType>& a);
        CPUMatrix<ElemType>& AssignElementProductOf (const CPUMatrix<ElemType>& a, const CPUMatrix<ElemType>& b);
        CPUMatrix<ElemType>& AddElementProductOf (const CPUMatrix<ElemType>& a, const CPUMatrix<ElemType>& b);

        CPUMatrix<ElemType>& AssignElementSumOf (size_t cCol, const CPUMatrix<ElemType>& a, size_t aColIdx, const CPUMatrix<ElemType>& b,  size_t bColIdx);

        CPUMatrix<ElemType>& AssignElementDivisionOf (const CPUMatrix<ElemType>& a, const CPUMatrix<ElemType>& b);

        CPUMatrix<ElemType>& ColumnElementMultiplyWith(const CPUMatrix<ElemType>& a);
        CPUMatrix<ElemType>& RowElementMultiplyWith(const CPUMatrix<ElemType>& a);

        CPUMatrix<ElemType>& ElementInverse ();
        CPUMatrix<ElemType>& AssignElementInverseOf (const CPUMatrix<ElemType>& a);

        CPUMatrix<ElemType>& InplaceSigmoid ();
        CPUMatrix<ElemType>& AssignSigmoidOf (const CPUMatrix<ElemType>& a);

        CPUMatrix<ElemType>& InplaceLinearRectifierDerivative();
        CPUMatrix<ElemType>& AssignLinearRectifierDerivativeOf (const CPUMatrix<ElemType>& a);

        CPUMatrix<ElemType>& InplaceSigmoidDerivative();
        CPUMatrix<ElemType>& AssignSigmoidDerivativeOf (const CPUMatrix<ElemType>& a);

        CPUMatrix<ElemType>& InplaceTanh ();
        CPUMatrix<ElemType>& AssignTanhOf (const CPUMatrix<ElemType>& a);

        CPUMatrix<ElemType>& InplaceSoftmax (const bool isColWise);
        CPUMatrix<ElemType>& AssignSoftmaxOf (const CPUMatrix<ElemType>& a, const bool isColWise);
        
        CPUMatrix<ElemType>& InplaceSqrt ();
        CPUMatrix<ElemType>& AssignSqrtOf (const CPUMatrix<ElemType>& a);

        CPUMatrix<ElemType>& InplaceExp ();
        CPUMatrix<ElemType>& AssignExpOf (const CPUMatrix<ElemType>& a);

        CPUMatrix<ElemType>& InplaceLog ();
        CPUMatrix<ElemType>& AssignLogOf (const CPUMatrix<ElemType>& a);

        CPUMatrix<ElemType>& InplaceLog10 ();
        CPUMatrix<ElemType>& AssignLog10Of (const CPUMatrix<ElemType>& a);

        CPUMatrix<ElemType>& InplaceCosine ();
        CPUMatrix<ElemType>& AssignCosineOf (const CPUMatrix<ElemType>& a);

        CPUMatrix<ElemType>& InplaceNegativeSine ();
        CPUMatrix<ElemType>& AssignNegativeSineOf (const CPUMatrix<ElemType>& a);

        CPUMatrix<ElemType>& InplaceAbs ();
        CPUMatrix<ElemType>& AssignAbsOf (const CPUMatrix<ElemType>& a);

        CPUMatrix<ElemType>& InplaceTruncateBottom (const ElemType threshold);
        CPUMatrix<ElemType>& AssignTruncateBottomOf (const CPUMatrix<ElemType>& a, const ElemType threshold);
        CPUMatrix<ElemType>& InplaceTruncateTop (const ElemType threshold);
        CPUMatrix<ElemType>& AssignTruncateTopOf (const CPUMatrix<ElemType>& a, const ElemType threshold);
        CPUMatrix<ElemType>& InplaceTruncate (const ElemType threshold);

        CPUMatrix<ElemType>& SetToZeroIfAbsLessThan (const ElemType threshold);

        ElemType SumOfAbsElements () const; //sum of all abs(elements)
        ElemType SumOfElements () const; //sum of all elements
        CPUMatrix<ElemType>& AssignSumOfElements(const CPUMatrix<ElemType>& a);

        bool IsEqualTo(const CPUMatrix<ElemType>& a, const ElemType threshold = 1e-8) const;

        void VectorNorm1(CPUMatrix<ElemType>& c, const bool isColWise) const;
        CPUMatrix<ElemType>& AssignVectorNorm1Of(CPUMatrix<ElemType>& a, const bool isColWise);

        void VectorNorm2(CPUMatrix<ElemType>& c, const bool isColWise) const;
        CPUMatrix<ElemType>& AssignVectorNorm2Of(CPUMatrix<ElemType>& a, const bool isColWise);

        void VectorNormInf(CPUMatrix<ElemType>& c, const bool isColWise) const;
        CPUMatrix<ElemType>& AssignVectorNormInfOf(CPUMatrix<ElemType>& a, const bool isColWise);

        CPUMatrix<ElemType>& AssignInnerProductOf(const CPUMatrix<ElemType>& a, const CPUMatrix<ElemType>& b, const bool isColWise);
        CPUMatrix<ElemType>& AssignKhatriRaoProductOf(const CPUMatrix<ElemType>& a, const CPUMatrix<ElemType>& b);
        CPUMatrix<ElemType>& AddColumnReshapeProductOf(const CPUMatrix<ElemType>& a, const CPUMatrix<ElemType>& b, const bool transposeAColumn);

        CPUMatrix<ElemType>& AddWithScaleOf(ElemType alpha, const CPUMatrix<ElemType>& a);

        ElemType FrobeniusNorm() const;
        CPUMatrix<ElemType>& AssignFrobeniusNormOf(const CPUMatrix<ElemType>& a);

        ElemType MatrixNormInf() const;
        ElemType MatrixNorm1() const;
        ElemType MatrixNorm0() const; //number of non-zero elemets
        CPUMatrix<ElemType>& AssignSignOf(const CPUMatrix<ElemType>& a);
        CPUMatrix<ElemType>& AddSignOf(const CPUMatrix<ElemType>& a);

        CPUMatrix<ElemType>&  AssignRowSliceValuesOf(const CPUMatrix<ElemType>& a, const size_t startIndex, const size_t numRows); 
        CPUMatrix<ElemType>&  AddToRowSliceValuesOf(const CPUMatrix<ElemType>& a, const size_t startIndex, const size_t numRows); 
        
        void VectorMax(CPUMatrix<ElemType>& maxIndexes, CPUMatrix<ElemType>& maxValues, const bool isColWise) const;
        void VectorMin(CPUMatrix<ElemType>& mainndexes, CPUMatrix<ElemType>& minValues, const bool isColWise) const;

        CPUMatrix<ElemType>&  AssignNumOfDiff(const CPUMatrix<ElemType>& a, const CPUMatrix<ElemType>& b); 

        void Print(const char* matrixName, size_t rowStart, size_t rowEnd, size_t colStart, size_t colEnd) const;
        void Print(const char* matrixName = nullptr) const; //print whole matrix. can be expensive

        void ReadFromFile(FILE* f, const char * matrixName); //matrixName is used to verify that correct matrix is read.
        void WriteToFile(FILE* f, const char * matrixName); //matrixName is used to verify that correct matrix is read.

        CPUMatrix<ElemType>&   AssignPackedConvolutionInput(const CPUMatrix<ElemType>& inputSubBatch, 
                                                 const size_t inputWidth, const size_t inputHeight, const size_t inputChannels,
                                                 const size_t outputWidth, const size_t outputHeight, const size_t outputChannels,
                                                 const size_t kernelWidth, const size_t kernelHeight, const size_t horizontalSubsample, const size_t verticalSubsample, 
                                                 const bool zeroPadding = false); 
        CPUMatrix<ElemType>&   UnpackConvolutionInput(CPUMatrix<ElemType>& inputSubBatch, 
                                                 const size_t inputWidth, const size_t inputHeight, const size_t inputChannels,
                                                 const size_t outputWidth, const size_t outputHeight, const size_t outputChannels,
                                                 const size_t kernelWidth, const size_t kernelHeight, const size_t horizontalSubsample, const size_t verticalSubsample, 
                                                 const bool zeroPadding = false) const; 
        CPUMatrix<ElemType>& AssignMaxPoolingResult(const CPUMatrix<ElemType>& inputBatch, const size_t channels, 
                                                 const size_t inputWidth, const size_t inputHeight, const size_t inputSizePerSample, 
                                                 const size_t outputWidth, const size_t outputHeight, const size_t outputSizePerSample, 
                                                 const size_t windowWidth, const size_t windowHeight, const size_t horizontalSubsample, const size_t verticalSubsample);
        CPUMatrix<ElemType>& AddMaxPoolingGradient(const CPUMatrix<ElemType>& outputGradientBatch, const CPUMatrix<ElemType>& inputBatch, const CPUMatrix<ElemType>& outputBatch, 
                                                 const size_t channels, 
                                                 const size_t inputWidth, const size_t inputHeight, const size_t inputSizePerSample, 
                                                 const size_t outputWidth, const size_t outputHeight, const size_t outputSizePerSample, 
                                                 const size_t windowWidth, const size_t windowHeight, const size_t horizontalSubsample, const size_t verticalSubsample);
        CPUMatrix<ElemType>& AssignAveragePoolingResult(const CPUMatrix<ElemType>& inputBatch, const size_t channels, 
                                                 const size_t inputWidth, const size_t inputHeight, const size_t inputSizePerSample, 
                                                 const size_t outputWidth, const size_t outputHeight, const size_t outputSizePerSample, 
                                                 const size_t windowWidth, const size_t windowHeight, const size_t horizontalSubsample, const size_t verticalSubsample);
        CPUMatrix<ElemType>& AddAveragePoolingGradient(const CPUMatrix<ElemType>& outputGradientBatch, 
                                                 const size_t channels, 
                                                 const size_t inputWidth, const size_t inputHeight, const size_t inputSizePerSample, 
                                                 const size_t outputWidth, const size_t outputHeight, const size_t outputSizePerSample, 
                                                 const size_t windowWidth, const size_t windowHeight, const size_t horizontalSubsample, const size_t verticalSubsample);

    public:

        //static BLAS functions
        static void SVD(const CPUMatrix<ElemType>& A, CPUMatrix<ElemType>& SIGMA, CPUMatrix<ElemType>& U, CPUMatrix<ElemType>& VT);

        static void MultiplyAndWeightedAdd(ElemType alpha, const CPUMatrix<ElemType>& a, const bool transposeA, const CPUMatrix<ElemType>& b, const bool transposeB, 
            ElemType beta, CPUMatrix<ElemType>& c);
        static void MultiplyAndAdd(const CPUMatrix<ElemType>& a, const bool transposeA, const CPUMatrix<ElemType>& b, const bool transposeB, CPUMatrix<ElemType>& c);
        static void Multiply(const CPUMatrix<ElemType>& a, const bool transposeA, const CPUMatrix<ElemType>& b, const bool transposeB, CPUMatrix<ElemType>& c);
        static void Multiply(const CPUMatrix<ElemType>& a, const CPUMatrix<ElemType>& b, CPUMatrix<ElemType>& c);

        static void ScaleAndAdd(ElemType alpha, const CPUMatrix<ElemType>& a, CPUMatrix<ElemType>& c);
        static void AddScaledDifference(const ElemType alpha, const CPUMatrix<ElemType>& a, const CPUMatrix<ElemType>& b, CPUMatrix<ElemType>& c);
        static void AssignScaledDifference(const ElemType alpha, const CPUMatrix<ElemType>& a, const CPUMatrix<ElemType>& b, CPUMatrix<ElemType>& c);
        static void AddScaledDifference(const CPUMatrix<ElemType>& alpha, const CPUMatrix<ElemType>& a, const CPUMatrix<ElemType>& b, CPUMatrix<ElemType>& c); //alpha must be 1X1
        static void AssignScaledDifference(const CPUMatrix<ElemType>& alpha, const CPUMatrix<ElemType>& a, const CPUMatrix<ElemType>& b, CPUMatrix<ElemType>& c); //alpha must be 1X1

        static void AddElementToElement(const CPUMatrix<ElemType>& a, const size_t ai, const size_t aj, CPUMatrix<ElemType>& c, const size_t ci, const size_t cj); 
        //static void AddLogElementToElement(const CPUMatrix<ElemType>& a, const size_t ai, const size_t aj, CPUMatrix<ElemType>& c, const size_t ci, const size_t cj); 
        static void AssignElementToElement(const CPUMatrix<ElemType>& a, const size_t ai, const size_t aj, CPUMatrix<ElemType>& c, const size_t ci, const size_t cj); 

        static void Scale(ElemType alpha, CPUMatrix<ElemType>& a);
        static void Scale(CPUMatrix<ElemType> alpha, CPUMatrix<ElemType>& a); //In this case Matrix alpha must be 1x1
        static void Scale(ElemType alpha, const CPUMatrix<ElemType>& a, CPUMatrix<ElemType>& c);
        static void InnerProduct (const CPUMatrix<ElemType>& a, const CPUMatrix<ElemType>& b, CPUMatrix<ElemType>& c, const bool isColWise);
        static ElemType InnerProductOfMatrices(const CPUMatrix<ElemType>& a, const CPUMatrix<ElemType>& b);
        static void ElementWisePower (ElemType alpha, const CPUMatrix<ElemType>& a, CPUMatrix<ElemType>& c);

        static bool AreEqual(const CPUMatrix<ElemType>& a, const CPUMatrix<ElemType>& b, const ElemType threshold = 1e-8);

        static CPUMatrix<ElemType> Ones(const size_t rows, const size_t cols);
        static CPUMatrix<ElemType> Zeros(const size_t rows, const size_t cols);
        static CPUMatrix<ElemType> Eye(const size_t rows);
        static CPUMatrix<ElemType> RandomUniform(const size_t rows, const size_t cols, const ElemType low, const ElemType high, unsigned long seed=USE_TIME_BASED_SEED);
        static CPUMatrix<ElemType> RandomGaussian(const size_t rows, const size_t cols, const ElemType mean, const ElemType sigma, unsigned long seed=USE_TIME_BASED_SEED);

    public:
        friend File& operator>>(File& stream, CPUMatrix<ElemType>& us)
        {
            stream.GetMarker(fileMarkerBeginSection, std::wstring(L"BMAT"));
            size_t elsize;
            stream>>elsize;
            if (sizeof(ElemType)!=elsize)
                throw std::exception("Template argument size doesn't match those in file");
            std::wstring matrixName;
            size_t numRows, numCols;
            int format;
            stream>>matrixName>>format>>numRows>>numCols;
            ElemType* d_array = new ElemType[numRows*numCols];
            for (size_t i=0;i<numRows*numCols;++i)
                stream>>d_array[i];
            stream.GetMarker(fileMarkerEndSection, std::wstring(L"EMAT"));
            us.SetValue(numRows,numCols,d_array, matrixFlagNormal);
            if (us.m_matrixName )
                delete [] us.m_matrixName ;
            us.m_matrixName = new wchar_t[matrixName.length()+1];
            wmemcpy(us.m_matrixName,matrixName.c_str(),matrixName.length()+1);

            delete[] d_array;
            return stream;
        }
        friend File& operator<<(File& stream, const CPUMatrix<ElemType>& us)
        {
            stream.PutMarker(fileMarkerBeginSection, std::wstring(L"BMAT"));
            stream<<sizeof(ElemType);
            
            std::wstring s = (us.m_matrixName==NULL)? std::wstring(L"unnamed") : std::wstring(us.m_matrixName);
            int format = us.m_format;
            stream<<s<<format;

            stream<<us.m_numRows<<us.m_numCols;
            for (size_t i=0;i<us.GetNumElements();++i)
                stream<<us.m_pArray[i];
            stream.PutMarker(fileMarkerEndSection, std::wstring(L"EMAT"));
            return stream;
        }


    protected:
	// Was inline.. but without definition, it doesn't make sense.
        size_t LocateElement (const size_t i, const size_t j) const;
        size_t LocateColumn (const size_t j) const;

    private:
        void ZeroInit(); //should only be used by constructors.
        void Clear();
    };

    typedef CPUMatrix<float> CPUSingleMatrix;
    typedef CPUMatrix<double> CPUDoubleMatrix;

}}}

