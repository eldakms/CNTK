﻿//
// <copyright file="CompositeComputationNode.h" company="Microsoft">
//     Copyright (c) Microsoft Corporation.  All rights reserved.
// </copyright>
//
#pragma once

//The basic idea of this implementation is learned from Brian Guenter <bguenter@microsoft.com>

#include <map>
#include <string>
#include <stdexcept>
#include <list>
#include "ComputationNode.h"
#include "TrainingCriterionNode.h"

//this file will contain computation nodes that require several atomic computation.
//composite nodes can save memory, computation, or both
namespace Microsoft { namespace MSR { namespace CNTK {

    template<class ElemType>
    class PreComputedNode : public ComputationNode<ElemType>  //this is a noninstantiable virtual class, all nodes require precomputation should derive from it
    {
        typedef ComputationNode<ElemType>* ComputationNodePtr; 


    public:
        PreComputedNode(short deviceId) : ComputationNode(deviceId) {}
        virtual bool HasComputed() const = 0;
        virtual void MarkComputed(const bool hasComputed) = 0;

        virtual bool RequirePreCompute() const { return true;}
                
        virtual void SaveToFile(File& fstream)  const
        {
            ComputationNode<ElemType>::SaveToFile(fstream);

            fstream << m_hasComputed;
            fstream << m_functionValues;
        }

        virtual void LoadFromFile(File& fstream, const size_t modelVersion, const short deviceId=AUTOPLACEMATRIX)
        {
            ComputationNode<ElemType>::LoadFromFile(fstream, modelVersion, deviceId);

            fstream >> m_hasComputed;
            fstream >> m_functionValues;
        }


        virtual void DumpNodeInfo(const bool printValues, File& fstream) const
        {
            ComputationNode<ElemType>::DumpNodeInfo(printValues, fstream);

            WCHAR str[4096];
            wsprintf(str, L"[%lu,%lu]  ", FunctionValues().GetNumRows(), FunctionValues().GetNumCols());
            fstream << wstring(str);
            wsprintf(str, L"HasComputed=%ws", HasComputed()? L"true" : L"false");
            fstream << wstring(str);

            PrintNodeValuesToFile(printValues, fstream);
        }

    protected:
        bool m_hasComputed;
    };

    template class PreComputedNode<float>; 
    template class PreComputedNode<double>;

    template<class ElemType>
    class MeanNode : public PreComputedNode<ElemType>
    {
        typedef ComputationNode<ElemType>* ComputationNodePtr; 


    public:
        MeanNode(const short deviceId=AUTOPLACEMATRIX, const std::wstring name = L"") : PreComputedNode(deviceId), ones(deviceId)  
        {
            m_nodeName = (name == L""? CreateUniqNodeName() : name);
            m_deviceId = deviceId;
            MoveMatricesToDevice(deviceId);
            m_hasComputed = false;
            m_numSamples = 0;
            InitRecurrentNode();
        }

        MeanNode(File& fstream, const size_t modelVersion, const short deviceId=AUTOPLACEMATRIX, const std::wstring name = L"") : PreComputedNode(deviceId), ones(deviceId)
        {
            m_nodeName = (name == L""? CreateUniqNodeName() : name);
            LoadFromFile(fstream, modelVersion, deviceId);
        }

        virtual void LoadFromFile(File& fstream, const size_t modelVersion, const short deviceId=AUTOPLACEMATRIX)
        {
            PreComputedNode<ElemType>::LoadFromFile(fstream, modelVersion, deviceId);

            m_numSamples = 0;
        }

        virtual bool HasComputed() const {return m_hasComputed;}
        virtual void MarkComputed(const bool hasComputed)
        {
            m_hasComputed = hasComputed;

            if (m_hasComputed && m_numSamples > 0)  //m_numSamples>0 means it's not called from model loading
            {
                m_numSamples = 0;
            }
            //Matrix<ElemType> &avg =FunctionValues();
            //avg.Print("mean",0,avg.GetNumRows()-1,0,avg.GetNumCols()-1);
        }

        virtual bool RequirePreCompute() const { return true;}

        virtual const std::wstring OperationName() const {return TypeName();}
        static const std::wstring TypeName() {return L"Mean";} 
        virtual void ComputeInputPartial(const size_t inputIndex)
        {
            throw std::logic_error("Mean operation should not be involved in the gradient calculation.");
        }

        virtual void ComputeInputPartial(const size_t inputIndex, const size_t timeIdxInSeq) 
        {
            throw std::logic_error("Mean operation should not be involved in the gradient calculation.");
        }

        virtual void EvaluateThisNode()  
        {
            if (!m_hasComputed)
            {
                Matrix<ElemType> &samples =Inputs(0)->FunctionValues();
                Matrix<ElemType> &avg =FunctionValues();
#if NANCHECK
                samples.HasNan("Mean-Samples");
#endif
                
                ones.SetPreferredDeviceId(samples.GetPreferredDeviceId());

                if (samples.GetNumCols() != ones.GetNumRows())
                {
                    ones.Resize(samples.GetNumCols(), 1);
                    ones.SetValue(1);

                }

                Matrix<ElemType>::MultiplyAndWeightedAdd(1.0f/(m_numSamples + samples.GetNumCols()),samples, false, ones, false, (ElemType)m_numSamples/(m_numSamples + samples.GetNumCols()), avg);
#if NANCHECK
                avg.HasNan("Mean-avg");
                ones.HasNan("Mean-ones");
#endif
                
                m_numSamples += samples.GetNumCols();

            }
        }
        virtual void EvaluateThisNode(const size_t timeIdxInSeq) 
        {
            throw std::logic_error("Mean operation should not be involved in a recurrent loop.");
        }

        virtual void Validate()
        {
            PrintSelfBeforeValidation();

            if (m_children.size() != 1) 
                throw std::logic_error("Mean operation should have one input.");

            if (Inputs(0)->FunctionValues().GetNumElements() == 0)
                throw std::logic_error("Mean operation: the input node has 0 element.");

            FunctionValues().Resize(Inputs(0)->FunctionValues().GetNumRows(), 1);
            CopyImageSizeFromInputs(); 
        }

        virtual void AttachInputs(const ComputationNodePtr singleInput) 
        {
            m_children.resize(1);
            m_children[0] = singleInput;
        }

        virtual void MoveMatricesToDevice(const short deviceId)
        {
            ComputationNode<ElemType>::MoveMatricesToDevice(deviceId);

            if (deviceId != AUTOPLACEMATRIX)
            {
                if (ones.GetDeviceId() != deviceId)
                    ones.TransferFromDeviceToDevice(ones.GetDeviceId(), deviceId);
            }
        }

        virtual void CopyTo(const ComputationNodePtr nodeP, const std::wstring& newName, const CopyNodeFlags flags) const
        {
            ComputationNode<ElemType>::CopyTo(nodeP, newName, flags);
            MeanNode<ElemType>* node = (MeanNode<ElemType>*) nodeP;

            if (flags & CopyNodeFlags::copyNodeValue)
            {
                node->m_hasComputed = m_hasComputed;
                node->m_numSamples = m_numSamples;
                node->ones = ones;
            }
        }

        // copy constructor
        MeanNode(const MeanNode<ElemType>* node, const std::wstring& newName, const CopyNodeFlags flags) 
            : PreComputedNode(node->m_deviceId), ones(node->m_deviceId)
        {
            node->CopyTo(this, newName, flags);
        }

        virtual ComputationNodePtr Duplicate(const std::wstring& newName, const CopyNodeFlags flags) const
        {
            const std::wstring& name = (newName == L"")?NodeName():newName;
                
            ComputationNodePtr node = new MeanNode<ElemType>(this, name, flags);
            return node;
        }

    private:
        size_t m_numSamples;
        Matrix<ElemType> ones;
    };

    template class MeanNode<float>; 
    template class MeanNode<double>;

    template<class ElemType>
    class InvStdDevNode : public PreComputedNode<ElemType>
    {
        typedef ComputationNode<ElemType>* ComputationNodePtr; 


    public:
        InvStdDevNode(const short deviceId=AUTOPLACEMATRIX, const std::wstring name = L"")
            : PreComputedNode(deviceId), avg(deviceId), avgsqr(deviceId), ones(deviceId), sampsqr(deviceId)
        {
            m_nodeName = (name == L""? CreateUniqNodeName() : name);
            m_deviceId = deviceId;
            MoveMatricesToDevice(deviceId);
            m_hasComputed = false;
            m_numSamples = 0;
            InitRecurrentNode();
        }

        InvStdDevNode(File& fstream, const size_t modelVersion, const short deviceId=AUTOPLACEMATRIX, const std::wstring name = L"")
            : PreComputedNode(deviceId), avg(deviceId), avgsqr(deviceId), ones(deviceId), sampsqr(deviceId)
        {
            m_nodeName = (name == L""? CreateUniqNodeName() : name);
            LoadFromFile(fstream, modelVersion, deviceId);
        }

        virtual void LoadFromFile(File& fstream, const size_t modelVersion, const short deviceId=AUTOPLACEMATRIX)
        {
            PreComputedNode<ElemType>::LoadFromFile(fstream, modelVersion, deviceId);

            m_numSamples = 0;
        }

        virtual bool HasComputed() const {return m_hasComputed;}

        virtual void MarkComputed(const bool hasComputed)
        {
            m_hasComputed = hasComputed;

            if (m_hasComputed && m_numSamples > 0)  //m_numSamples>0 means it's not called from model loading
            {
                ElemType sqrtFloor = 1e-10f;

#if NANCHECK
                avg.HasNan("MarkComputed-avg");
#endif
                avg ^= 2;
#if NANCHECK
                avg.HasNan("MarkComputed-avg^2");
#endif
                avgsqr -= avg;
#if NANCHECK
                avgsqr.HasNan("MarkComputed-(avgsqr-avg)");
#endif
                // do a floor here, because we can get small negative numbers that will turn into QNAN if they make it to the SQRT call below
                avgsqr.InplaceTruncateBottom(sqrtFloor); //prevent too small variance (and negative square roots)
#if NANCHECK
                avgsqr.HasNan("MarkComputed-InplaceTruncateBottom");
#endif
                avgsqr.InplaceSqrt();
#if NANCHECK
                avgsqr.HasNan("MarkComputed-InplaceSqrt");
#endif
                avgsqr.ElementInverse();
#if NANCHECK
                avgsqr.HasNan("MarkComputed-ElementInverse()");
#endif
                FunctionValues().SetValue(avgsqr);

                m_numSamples = 0;

                //Matrix<ElemType> &invstddev =FunctionValues();
                //invstddev.Print("invstddev",0,invstddev.GetNumRows()-1,0,invstddev.GetNumCols()-1);
            }
        }
        virtual bool RequirePreCompute() const { return true;}

        virtual const std::wstring OperationName() const {return TypeName();}
        static const std::wstring TypeName() {return L"InvStdDev";} 
        virtual void ComputeInputPartial(const size_t inputIndex)
        {
            throw std::logic_error("InvStdDev operation should not be involved in the gradient calculation.");
        }

        virtual void ComputeInputPartial(const size_t inputIndex, const size_t timeIdxInSeq) 
        {
            throw std::logic_error("InvStdDev operation should not be involved in the gradient calculation.");
        }

        virtual void EvaluateThisNode()  
        {
            if (!m_hasComputed)
            {
                Matrix<ElemType> &samples = Inputs(0)->FunctionValues();
#if NANCHECK
                samples.HasNan("InvStdDev-Samples");
#endif

                ones.SetPreferredDeviceId(samples.GetPreferredDeviceId());
                sampsqr.SetPreferredDeviceId(samples.GetPreferredDeviceId());

                if (samples.GetNumCols() != ones.GetNumRows())
                {
					//ones._decideDevices(ones); // if it's going to move do it before we resize
                    ones.Resize(samples.GetNumCols(), 1);
                    ones.SetValue(1);
                }

                if (samples.GetNumCols() != sampsqr.GetNumCols() || samples.GetNumRows() != sampsqr.GetNumRows())
                {
					//sampsqr._decideDevices(sampsqr); // if it's going to move do it before we resize
                    sampsqr.Resize(samples.GetNumRows(), samples.GetNumCols());
                    sampsqr.SetValue(1); // value not needed, but need to get it to correct device (handled by SetValue())
                }

                Matrix<ElemType>::MultiplyAndWeightedAdd(1.0f/(m_numSamples + samples.GetNumCols()),samples, false, ones, false, (ElemType)m_numSamples/(m_numSamples + samples.GetNumCols()), avg);

                sampsqr = samples^2;

                Matrix<ElemType>::MultiplyAndWeightedAdd(1.0f/(m_numSamples + sampsqr.GetNumCols()), sampsqr, false, ones, false, (ElemType)m_numSamples/(m_numSamples + samples.GetNumCols()), avgsqr);

#if NANCHECK
                avgsqr.HasNan("InvStdDev-avgsqr");
#endif

                m_numSamples += samples.GetNumCols();
            }
        }

        virtual void EvaluateThisNode(const size_t timeIdxInSeq) 
        {
            throw std::logic_error("InvStdDev operation should not be involved in a recurrent loop.");
        }

        virtual void Validate()
        {
            PrintSelfBeforeValidation();

            if (m_children.size() != 1) 
                throw std::logic_error("InvStdDev operation should have one input.");

            if (Inputs(0)->FunctionValues().GetNumElements() == 0)
                throw std::logic_error("InvStdDev operation: the input node has 0 element.");

            size_t inputDim = Inputs(0)->FunctionValues().GetNumRows();
            avg.Resize(inputDim, 1);        
            avgsqr.Resize(inputDim, 1); 

            FunctionValues().Resize(inputDim, 1);
            CopyImageSizeFromInputs(); 
        }

        virtual void AttachInputs(const ComputationNodePtr singleInput) 
        {
            m_children.resize(1);
            m_children[0] = singleInput;
        }

        virtual void MoveMatricesToDevice(const short deviceId)
        {
            ComputationNode<ElemType>::MoveMatricesToDevice(deviceId);

            if (deviceId != AUTOPLACEMATRIX)
            {
                if (avg.GetDeviceId() != deviceId)
                    avg.TransferFromDeviceToDevice(avg.GetDeviceId(), deviceId);
                if (avgsqr.GetDeviceId() != deviceId)
                    avgsqr.TransferFromDeviceToDevice(avgsqr.GetDeviceId(), deviceId);
                if (ones.GetDeviceId() != deviceId)
                    ones.TransferFromDeviceToDevice(ones.GetDeviceId(), deviceId);
				if (sampsqr.GetDeviceId() != deviceId)
                    sampsqr.TransferFromDeviceToDevice(sampsqr.GetDeviceId(), deviceId);

            }
        }

        virtual void CopyTo(const ComputationNodePtr nodeP, const std::wstring& newName, const CopyNodeFlags flags) const
        {
            ComputationNode<ElemType>::CopyTo(nodeP, newName, flags);
            InvStdDevNode<ElemType>* node = (InvStdDevNode<ElemType>*) nodeP;

            if (flags & CopyNodeFlags::copyNodeValue)
            {
                node->m_hasComputed = m_hasComputed;
                node->m_numSamples = m_numSamples;

                node->avg = avg;
                node->avgsqr = avgsqr;
                node->ones = ones;
                node->sampsqr = sampsqr;
            }
        }

        // copy constructor
        InvStdDevNode(const InvStdDevNode<ElemType>* node, const std::wstring& newName, const CopyNodeFlags flags) 
            : PreComputedNode(node->m_deviceId), avg(node->m_deviceId), avgsqr(node->m_deviceId), ones(node->m_deviceId), sampsqr(node->m_deviceId)
        {                                                                                                                                               
            node->CopyTo(this, newName, flags);                                                                                                         
        }                                                                                                                                               

        virtual ComputationNodePtr Duplicate(const std::wstring& newName, const CopyNodeFlags flags) const
        {
            const std::wstring& name = (newName == L"")?NodeName():newName;
                
            ComputationNodePtr node = new InvStdDevNode<ElemType>(this, name, flags);
            return node;
        }

    private:
        size_t m_numSamples;
        Matrix<ElemType> avg;
        Matrix<ElemType> avgsqr;
        Matrix<ElemType> ones;
        Matrix<ElemType> sampsqr;
    };

    template class InvStdDevNode<float>;
    template class InvStdDevNode<double>;

    template<class ElemType>
    class PerDimMeanVarNormalizationNode : public ComputationNode<ElemType>
    {
        typedef ComputationNode<ElemType>* ComputationNodePtr; 


    public:
        PerDimMeanVarNormalizationNode(const short deviceId=AUTOPLACEMATRIX, const std::wstring name = L"") : ComputationNode(deviceId) 
        {
            m_nodeName = (name == L""? CreateUniqNodeName() : name);
            m_deviceId = deviceId;
            MoveMatricesToDevice(deviceId);
            InitRecurrentNode();
        }

        PerDimMeanVarNormalizationNode(File& fstream, const size_t modelVersion, const short deviceId=AUTOPLACEMATRIX, const std::wstring name = L"") : ComputationNode(deviceId)
        {
            m_nodeName = (name == L""? CreateUniqNodeName() : name);
            LoadFromFile(fstream, modelVersion, deviceId);
        }

        // copy constructor
        PerDimMeanVarNormalizationNode(const PerDimMeanVarNormalizationNode<ElemType>* node, const std::wstring& newName, const CopyNodeFlags flags) : ComputationNode(node->m_deviceId)
        {
            node->CopyTo(this, newName, flags);
        }

        virtual ComputationNodePtr Duplicate(const std::wstring& newName, const CopyNodeFlags flags) const
        {
            const std::wstring& name = (newName == L"")?NodeName():newName;
                
            ComputationNodePtr node = new PerDimMeanVarNormalizationNode<ElemType>(this, name, flags);
            return node;
        }

        virtual const std::wstring OperationName() const {return TypeName();}
        static const std::wstring TypeName() {return L"PerDimMeanVarNormalization";} 
        virtual void ComputeInputPartial(const size_t inputIndex)  //scaled by 2*number of colmns (samples) in the Matrix<ElemType>
        {
            throw std::invalid_argument("PerDimMeanVarNormalizationNode should only be called in the evaluation stage.");
        }

        virtual void ComputeInputPartial(const size_t inputIndex, const size_t timeIdxInSeq)
        {
            throw std::invalid_argument("PerDimMeanVarNormalizationNode should only be called in the evaluation stage.");
        }

        // GetTaskDescriptor - Get a task descriptor for this node
        // taskType - task type we are generating a task for
        virtual TaskDescriptor<ElemType>* GetPTaskDescriptor(TaskType taskType, size_t inputIndex=0) const
        {
            TaskDescriptor<ElemType>* descriptor = new TaskDescriptor<ElemType>(this, taskType, inputIndex);
            switch(taskType)
            {
            case taskEvaluate:
                descriptor->FunctionParam();
                descriptor->FunctionParam(0, paramOptionsInput);
                descriptor->FunctionParam(1, paramOptionsInput | paramOptionsConstant);
                descriptor->FunctionParam(2, paramOptionsInput | paramOptionsConstant);
                descriptor->SetFunction((FARPROC)EvaluateThisNodeS);
                break;
            default:
                assert(false);
                throw std::logic_error("Unsupported task requested");
            }
            return descriptor;
        }

        virtual void EvaluateThisNode()   //(feature-mean).*InvStdDev
        {
            EvaluateThisNodeS(FunctionValues(), Inputs(0)->FunctionValues(), Inputs(1)->FunctionValues(), Inputs(2)->FunctionValues());
        }

        virtual void EvaluateThisNode(const size_t timeIdxInSeq)
        {
            //only feature (input0) and output needs to be sliced
            Matrix<ElemType> sliceInput0Value = Inputs(0)->FunctionValues().ColumnSlice(timeIdxInSeq * m_samplesInRecurrentStep, m_samplesInRecurrentStep);
            Matrix<ElemType> sliceOutputValue = m_functionValues.ColumnSlice(timeIdxInSeq * m_samplesInRecurrentStep, m_samplesInRecurrentStep);

            EvaluateThisNodeS(sliceOutputValue, sliceInput0Value, Inputs(1)->FunctionValues(), Inputs(2)->FunctionValues());
        }

        static void WINAPI EvaluateThisNodeS(Matrix<ElemType> &functionValues, const Matrix<ElemType> &input0, const Matrix<ElemType> &input1, const Matrix<ElemType> &input2)
        {
#if DUMPOUTPUT
            //input0.Print("PerDimMeanVarNormalization-input0");
            //input1.Print("PerDimMeanVarNormalization-input1");
            //input2.Print("PerDimMeanVarNormalization-input2");
#endif

#if NANCHECK
            input0.HasNan("PerDimMeanVarNormalization-input0");
            input1.HasNan("PerDimMeanVarNormalization-input1");
            input2.HasNan("PerDimMeanVarNormalization-input2");
#endif
            functionValues.AssignDifferenceOf(input0, input1);
            functionValues.ColumnElementMultiplyWith(input2);
#if NANCHECK
            functionValues.HasNan("PerDimMeanVarNormalization");
#endif
#if DUMPOUTPUT
            functionValues.Print("PerDimMeanVarNormalizationNode");
#endif
        }

        virtual void Validate()
        {
            PrintSelfBeforeValidation();

            if (m_children.size() != 3) 
                throw std::logic_error("PerDimMeanVarNormalizationNode criterion requires three inputs.");

            if (Inputs(0)->RequirePreCompute())
                throw std::logic_error("PerDimMeanVarNormalizationNode criterion forbids first input from being a pre-compute node. "
                                       "The first input should be the node whose output should be normalized, and the second and third inputs "
                                       "should be LearnableParameter type or (Mean, InvStdDev) so that the values will be saved.");

            if (!(Inputs(1)->OperationName() == LearnableParameter<ElemType>::TypeName() && Inputs(2)->OperationName() == LearnableParameter<ElemType>::TypeName()) &&
                !(Inputs(1)->OperationName() == MeanNode<ElemType>::TypeName() && Inputs(2)->OperationName() == InvStdDevNode<ElemType>::TypeName()))
                throw std::logic_error("PerDimMeanVarNormalizationNode criterion requires the last two inputs to be LearnableParameter type or (Mean, InvStdDev) so that the values will be saved.");

            if (Inputs(1)->OperationName() == LearnableParameter<ElemType>::TypeName())
            {
                size_t rows = Inputs(1)->FunctionValues().GetNumRows() == 0? Inputs(0)->FunctionValues().GetNumRows() : Inputs(1)->FunctionValues().GetNumRows();
                Inputs(1)->FunctionValues().Resize(rows, 1);
            }

            if (Inputs(2)->OperationName() == LearnableParameter<ElemType>::TypeName())
            {
                size_t rows = Inputs(2)->FunctionValues().GetNumRows() == 0? Inputs(0)->FunctionValues().GetNumRows() : Inputs(2)->FunctionValues().GetNumRows();
                Inputs(2)->FunctionValues().Resize(rows, 1);
            }

            if (Inputs(0)->FunctionValues().GetNumElements() == 0 || Inputs(1)->FunctionValues().GetNumElements() == 0 || Inputs(2)->FunctionValues().GetNumElements() == 0)
                throw std::logic_error("PerDimMeanVarNormalizationNode operation: one of the operants has 0 element.");

            if (!(Inputs(0)->FunctionValues().GetNumRows() == Inputs(1)->FunctionValues().GetNumRows()  &&  //match rows
                Inputs(2)->FunctionValues().GetNumRows() == Inputs(1)->FunctionValues().GetNumRows()) )
            {
                //Inputs(1)->FunctionValues().Resize(Inputs(0)->FunctionValues().GetNumRows(), 1);
                //Inputs(2)->FunctionValues().Resize(Inputs(0)->FunctionValues().GetNumRows(), 1);
                throw std::logic_error("PerDimMeanVarNormalizationNode: All inputs should have same number of rows.");
            }       

            if (!(Inputs(1)->FunctionValues().GetNumCols() == 1 && Inputs(2)->FunctionValues().GetNumCols() == 1))
            {
                throw std::logic_error("PerDimMeanVarNormalizationNode: Mean and InvStdDev should be a colum  vector.");
            }       

            Inputs(1)->NeedGradient() = false;
            Inputs(2)->NeedGradient() = false;  //prevent learning 
            FunctionValues().Resize(Inputs(0)->FunctionValues().GetNumRows(), Inputs(0)->FunctionValues().GetNumCols());
            CopyImageSizeFromInputs(); 
        }

        //leftNode should be the empirical
        virtual void AttachInputs(const ComputationNodePtr feature, const ComputationNodePtr mean, const ComputationNodePtr InvStdDev) 
        {
            m_children.resize(3);
            m_children[0] = feature;
            m_children[1] = mean;
            m_children[2] = InvStdDev;
        }
    };

    template class PerDimMeanVarNormalizationNode<float>; 
    template class PerDimMeanVarNormalizationNode<double>;


    // convolution parameters structure, to make it easier to pass these around all these parameters
    struct ConvolutionParams
    {
        size_t inputWidth, inputHeight, inputChannels;
        size_t kernelWidth, kernelHeight;
        size_t horizontalSubsample, verticalSubsample;
        size_t outputWidth, outputHeight, outputChannels;
        size_t maxTempMemSizeInSamples;
        bool zeroPadding;
    };

    //convolutional network 
    //follow "high performance convolutional neural networks for document processing" by Kumar chellapilla, Sidde Puri, and Patrice Simard
    //assume each column is an input sample. Each sample is stored in [channel, row, col]  (r00, g00, b00, r01, g01, b01, r10, g10, b10, r11, g11, b11)
    template<class ElemType>
    class ConvolutionNode : public ComputationNode<ElemType>
    {
        typedef ComputationNode<ElemType>* ComputationNodePtr; 

    public:
        ConvolutionNode(const size_t kernelWidth, const size_t kernelHeight, const size_t outputChannels, 
                        const size_t horizontalSubsample, const size_t verticalSubsample, 
                        const bool zeroPadding = false, 
                        const short deviceId=AUTOPLACEMATRIX, const std::wstring name = L"",
                        const size_t maxTempMemSizeInSamples = 0)
                        : ComputationNode(deviceId), m_tempMatrix(deviceId),
                          m_kernelWidth(kernelWidth), m_kernelHeight(kernelHeight), 
                          m_horizontalSubsample(horizontalSubsample), m_verticalSubsample(verticalSubsample),
                          m_zeroPadding(zeroPadding), m_maxTempMemSizeInSamples(maxTempMemSizeInSamples)
        {
            m_outputChannels = outputChannels;
            m_nodeName = (name == L""? CreateUniqNodeName() : name);
            m_deviceId = deviceId;
            MoveMatricesToDevice(deviceId);
            InitRecurrentNode();
        }

        ConvolutionNode(File& fstream, const size_t modelVersion, const short deviceId=AUTOPLACEMATRIX, const std::wstring name = L"") 
            : ComputationNode(deviceId), m_tempMatrix(deviceId)
        {
            m_nodeName = (name == L""? CreateUniqNodeName() : name);
            LoadFromFile(fstream, modelVersion, deviceId);
        }
                
        virtual void SaveToFile(File& fstream) const
        {
            ComputationNode<ElemType>::SaveToFile(fstream);

            fstream <<  m_kernelWidth << m_kernelHeight << m_horizontalSubsample << m_verticalSubsample; 
            fstream << m_outputChannels << m_zeroPadding << m_maxTempMemSizeInSamples; 
        }

        virtual void LoadFromFile(File& fstream, const size_t modelVersion, const short deviceId=AUTOPLACEMATRIX)
        {
            ComputationNode<ElemType>::LoadFromFile(fstream, modelVersion, deviceId);

            fstream >>  m_kernelWidth >> m_kernelHeight >> m_horizontalSubsample >> m_verticalSubsample; 
            fstream >> m_outputChannels >> m_zeroPadding >> m_maxTempMemSizeInSamples; 
        }

       virtual void CopyTo(const ComputationNodePtr nodeP, const std::wstring& newName, const CopyNodeFlags flags) const
        {
            ComputationNode<ElemType>::CopyTo(nodeP, newName, flags);
            ConvolutionNode<ElemType>* node = (ConvolutionNode<ElemType>*) nodeP;

            if (flags & CopyNodeFlags::copyNodeValue)
            {
                node->m_kernelWidth = m_kernelWidth;
                node->m_kernelHeight = m_kernelHeight;

                node->m_horizontalSubsample = m_horizontalSubsample;
                node->m_verticalSubsample = m_verticalSubsample;

                node->m_zeroPadding = m_zeroPadding;

                node->m_maxTempMemSizeInSamples = m_maxTempMemSizeInSamples;

                node->m_tempMatrix = m_tempMatrix;
            }
        }

               // copy constructor
        ConvolutionNode(const ConvolutionNode<ElemType>* node, const std::wstring& newName, const CopyNodeFlags flags) 
            : ComputationNode(node->m_deviceId), m_tempMatrix(node->m_deviceId)
        {
            node->CopyTo(this, newName, flags);
        }

        virtual ComputationNodePtr Duplicate(const std::wstring& newName, const CopyNodeFlags flags) const
        {
            const std::wstring& name = (newName == L"")?NodeName():newName;
                
            ComputationNodePtr node = new ConvolutionNode<ElemType>(this, name, flags);
            return node;
        }

        virtual const std::wstring OperationName() const {return TypeName();}
        static const std::wstring TypeName() {return L"Convolution";} 

        ConvolutionParams GetConvolutionParams() const
        {
            ConvolutionParams convParam;
            convParam.inputWidth = m_inputWidth;
            convParam.inputHeight = m_inputHeight;
            convParam.inputChannels = m_inputChannels;

            convParam.kernelWidth = m_kernelWidth;
            convParam.kernelHeight = m_kernelHeight;

            convParam.horizontalSubsample = m_horizontalSubsample;
            convParam.verticalSubsample = m_verticalSubsample;

            convParam.outputWidth = m_outputWidth;
            convParam.outputHeight = m_outputHeight;
            convParam.outputChannels = m_outputChannels;

            convParam.zeroPadding = m_zeroPadding;

            convParam.maxTempMemSizeInSamples = m_maxTempMemSizeInSamples;
            return convParam;
        }

        virtual void ComputeInputPartial(const size_t inputIndex) 
        {
            if (inputIndex > 1)
                throw std::invalid_argument("Convolution operation only takes two inputs.");

            if (inputIndex == 0)  //derivative with regard to the weight matrix
            {
                ComputeInputPartialOverWeight(this, GradientValues(), Inputs(0)->GradientValues(), Inputs(0)->FunctionValues(), Inputs(1)->FunctionValues(), m_tempMatrix, true);
            }
            else  // derivative with regard to the input feature
            {
                ComputeInputPartialOverInputFeature(this, GradientValues(), Inputs(1)->GradientValues(), Inputs(0)->FunctionValues(), Inputs(1)->FunctionValues(), m_tempMatrix);
            }
        }

        virtual void ComputeInputPartial(const size_t inputIndex, const size_t timeIdxInSeq) 
        {
            if (inputIndex > 1)
                throw std::invalid_argument("Convolution operation only takes two inputs.");

            Matrix<ElemType> sliceOutputGrad = GradientValues().ColumnSlice(timeIdxInSeq * m_samplesInRecurrentStep, m_samplesInRecurrentStep);
            Matrix<ElemType> sliceInput1Value = Inputs(1)->FunctionValues().ColumnSlice(timeIdxInSeq * m_samplesInRecurrentStep, m_samplesInRecurrentStep);

            if (inputIndex == 0)  //derivative with regard to the weight matrix
            {
                ComputeInputPartialOverWeight(this, sliceOutputGrad, Inputs(0)->GradientValues(), Inputs(0)->FunctionValues(), sliceInput1Value, m_tempMatrix);
            }
            else  // derivative with regard to the input feature
            {
                Matrix<ElemType> sliceInput1Grad = Inputs(1)->GradientValues().ColumnSlice(timeIdxInSeq * m_samplesInRecurrentStep, m_samplesInRecurrentStep);
                ComputeInputPartialOverInputFeature(this, sliceOutputGrad, sliceInput1Grad, Inputs(0)->FunctionValues(), sliceInput1Value, m_tempMatrix);
            }
        }

        // GetTaskDescriptor - Get a task descriptor for this node
        // taskType - task type we are generating a task for
        virtual TaskDescriptor<ElemType>* GetPTaskDescriptor(TaskType taskType, size_t inputIndex=0) const
        {
            TaskDescriptor<ElemType>* descriptor = new TaskDescriptor<ElemType>(this, taskType, inputIndex);
            switch(taskType)
            {
            case taskComputeInputPartial:
                descriptor->Param(paramTypeNode, "ConvolutionNodePointer", paramOptionsInput | paramOptionsConstant);
                descriptor->GradientParam();
                descriptor->GradientParam(inputIndex, paramOptionsInput | paramOptionsOutput | paramOptionsInitialize);
                descriptor->FunctionParam(0, paramOptionsInput);
                descriptor->FunctionParam(1, paramOptionsInput);
                descriptor->MatrixParam(m_tempMatrix, "tempMatrix", paramOptionsOutput);
                descriptor->SetFunction(inputIndex==0?(FARPROC)ComputeInputPartialOverWeight:(FARPROC)ComputeInputPartialOverInputFeature);
                break;
            case taskEvaluate:
                descriptor->Param(paramTypeNode, "ConvolutionNodePointer", paramOptionsInput | paramOptionsConstant);
                descriptor->FunctionParam();
                descriptor->FunctionParam(0, paramOptionsInput);
                descriptor->FunctionParam(1, paramOptionsInput);
                descriptor->MatrixParam(m_tempMatrix, "tempMatrix", paramOptionsInput);
                descriptor->SetFunction((FARPROC)EvaluateThisNodeS);
                break;
            default:
                assert(false);
                throw std::logic_error("Unsupported task requested");
            }
            return descriptor;
        }

        virtual void EvaluateThisNode()  
        {
            EvaluateThisNodeS(this, FunctionValues(), Inputs(0)->FunctionValues(), Inputs(1)->FunctionValues(), m_tempMatrix);
        }

        virtual void EvaluateThisNode(const size_t timeIdxInSeq) 
        {
            Matrix<ElemType> sliceInput1Value = Inputs(1)->FunctionValues().ColumnSlice(timeIdxInSeq * m_samplesInRecurrentStep, m_samplesInRecurrentStep);
            Matrix<ElemType> sliceOutputValue = m_functionValues.ColumnSlice(timeIdxInSeq * m_samplesInRecurrentStep, m_samplesInRecurrentStep);

            EvaluateThisNodeS(this, sliceOutputValue, Inputs(0)->FunctionValues(), sliceInput1Value, m_tempMatrix);
        }

        static void WINAPI EvaluateThisNodeS(const ConvolutionNode<ElemType>* pConv, Matrix<ElemType> &functionValues, const Matrix<ElemType> &input0, 
            const Matrix<ElemType> &input1, Matrix<ElemType> &tempMatrix)
        {
#if NANCHECK
            input0.HasNan("Convolution-input0");
            input1.HasNan("Convolution-input1");
#endif
            ConvolutionParams convParam = pConv->GetConvolutionParams();

            size_t packedInputRows = convParam.kernelWidth * convParam.kernelHeight * convParam.inputChannels;
            size_t packedInputColsPerSample = convParam.outputWidth * convParam.outputHeight;
            size_t outputSizePerChannel = packedInputColsPerSample;
            size_t packedInputDim = packedInputRows * packedInputColsPerSample; // size of each packed input sample
            size_t inputDim = convParam.inputWidth * convParam.inputHeight * convParam.inputChannels;  //size of each input sample

            long batchSize = (long)input1.GetNumCols();  //right child is the input sample

            long maxTempMemSizeInSamples = (long)(convParam.maxTempMemSizeInSamples == 0? batchSize : convParam.maxTempMemSizeInSamples);

            const Matrix<ElemType> & weightMatrix = input0;
            assert(weightMatrix.GetNumCols() == packedInputRows && weightMatrix.GetNumRows() == convParam.outputChannels);
            functionValues.Resize(convParam.outputChannels, outputSizePerChannel * batchSize);

            long subBatchSize = (long)min(batchSize, maxTempMemSizeInSamples); 
            long numSubBatches = (batchSize+subBatchSize-1)/subBatchSize; 

            for (long i=0; i<numSubBatches; i++) 
            {
                long startSampleID = i*subBatchSize; 
                long endSampleID = min(batchSize, startSampleID + subBatchSize); 
                long smallBatchSize = endSampleID-startSampleID; 

                tempMatrix.Resize(packedInputRows, packedInputColsPerSample * smallBatchSize);
                Matrix<ElemType>  inputSubBatch = input1.ColumnSlice(startSampleID, smallBatchSize);
                tempMatrix.AssignPackedConvolutionInput(inputSubBatch, 
                                                                 convParam.inputWidth, convParam.inputHeight, convParam.inputChannels,
                                                                 convParam.outputWidth, convParam.outputHeight, convParam.outputChannels,
                                                                 convParam.kernelWidth, convParam.kernelHeight, convParam.horizontalSubsample, convParam.verticalSubsample, 
                                                                 convParam.zeroPadding); 

                Matrix<ElemType>  outputSubBatch = functionValues.ColumnSlice(outputSizePerChannel * startSampleID, outputSizePerChannel * smallBatchSize);
                Matrix<ElemType>::Multiply(weightMatrix, false, tempMatrix, false, outputSubBatch);
            }

            functionValues.Reshape(convParam.outputChannels * outputSizePerChannel, batchSize);  //each sample becomes a column

#if NANCHECK
            functionValues.HasNan("Convolution");
#endif
        }

        virtual void Validate()
        {
            PrintSelfBeforeValidation();

            if (m_children.size() != 2) 
                throw std::logic_error("ConvolutionNode requires two inputs.");

            //we may want to remove this check in the future if we want to support the case that the weight itself is result of some computation 
            //if (Inputs(0)->OperationName() != LearnableParameter<ElemType>::TypeName())
            //    throw std::logic_error("ConvolutionNode requires the first input to be LearnableParameter type.");

            if (m_horizontalSubsample > m_kernelWidth || m_verticalSubsample > m_kernelHeight)
                throw std::invalid_argument("In ConvolutionNode horizontalSubsample must <= kernelWidth and verticalSubsample must <= kernelHeight.");

            CopyImageSizeFromInputs();

            size_t weightCols = m_kernelWidth * m_kernelHeight * m_inputChannels;

            if (Inputs(0)->OperationName() == LearnableParameter<ElemType>::TypeName() && Inputs(0)->FunctionValues().GetNumElements() == 0)
            {
                Inputs(0)->FunctionValues().Resize(m_outputChannels, weightCols);
            }

            if (m_children[0]->FunctionValues().GetNumCols() != weightCols || m_children[0]->FunctionValues().GetNumRows() != m_outputChannels)
            {
                msra::strfun::strprintf msg("convolutionWeight matrix %ws should have dimension [%d, %d] which is [outputChannels, kernelWidth * kernelHeight * inputChannels]", 
                    m_children[0]->NodeName().c_str(), m_outputChannels, weightCols);
                throw std::logic_error(msg.c_str());            
            }

            size_t inputDim = m_inputWidth * m_inputHeight * m_inputChannels;
            if (Inputs(1)->OperationName() == LearnableParameter<ElemType>::TypeName() && Inputs(1)->FunctionValues().GetNumRows() == 0)
            {
                Inputs(1)->FunctionValues().Resize(inputDim, Inputs(1)->FunctionValues().GetNumCols());
            }

            if (m_children[1]->FunctionValues().GetNumRows() != inputDim)
            {
                msra::strfun::strprintf msg("each column of input to the convolution node %ws is a sample and should have dimension %d, which is inputWidth * inputHeight * inputChannels", 
                    NodeName().c_str(), inputDim);
                throw std::logic_error(msg.c_str());            
            }

            if (Inputs(0)->FunctionValues().GetNumElements() == 0 || Inputs(1)->FunctionValues().GetNumElements() == 0 )
                throw std::logic_error("Convolution operation: one of the operants has 0 element.");
            
            size_t outputDim = m_outputWidth * m_outputHeight * m_outputChannels;
            FunctionValues().Resize(outputDim, m_children[1]->FunctionValues().GetNumCols());
        }

        virtual void CopyImageSizeFromInputs()
        {
            CopyImageSizeFromInput(1, false);

            if (m_inputWidth < m_kernelWidth || m_inputHeight < m_kernelHeight)
                throw std::invalid_argument("inputWidth must >= kernelWidth and inputHeight must >= kernelHeight.");

            if (m_zeroPadding)
            {
                const int kernelWidthCenter = m_kernelWidth % 2;
                const int kernelHeightCenter = m_kernelHeight % 2;
                m_outputWidth = (m_inputWidth-kernelWidthCenter)/m_horizontalSubsample + 1;
                m_outputHeight = (m_inputHeight-kernelHeightCenter)/m_verticalSubsample + 1;
            }
            else
            {
                m_outputWidth = (m_inputWidth-m_kernelWidth)/m_horizontalSubsample + 1;
                m_outputHeight = (m_inputHeight-m_kernelHeight)/m_verticalSubsample + 1;
            }    
        }

        virtual void AttachInputs(const ComputationNodePtr convolutionWeight, const ComputationNodePtr inputFeature) 
        {
            m_children.resize(2);
            m_children[0] = convolutionWeight;
            m_children[1] = inputFeature;
        }

        virtual void MoveMatricesToDevice(const short deviceId)
        {
            ComputationNode<ElemType>::MoveMatricesToDevice(deviceId);

            if (deviceId != AUTOPLACEMATRIX)
            {
                if (m_tempMatrix.GetDeviceId() != deviceId)
                    m_tempMatrix.TransferFromDeviceToDevice(m_tempMatrix.GetDeviceId(), deviceId);
            }
        }

        virtual void DumpNodeInfo(const bool printValues, File& fstream) const
        {
            ComputationNode<ElemType>::DumpNodeInfo(printValues, fstream);

            WCHAR str[4096];
            wsprintf(str, L"Input[Width:%lu, Height:%lu, Channels:%lu]  \n", m_inputWidth, m_inputHeight, m_inputChannels);
            fstream << wstring(str);
            wsprintf(str, L"Kernel[Width:%lu, Height:%lu]  SubSample[Horizontal:%lu, Vertical:%lu]\n", m_kernelWidth, m_kernelHeight, m_horizontalSubsample, m_verticalSubsample);
            fstream << wstring(str);
            wsprintf(str, L"Output[Width:%lu, Height:%lu, Channels:%lu]  \n", m_outputWidth, m_outputHeight, m_outputChannels);
            fstream << wstring(str);
            wsprintf(str, L"ZeroPadding=%ws  maxTempMemSizeInSamples=%lu\n", m_zeroPadding? L"true" : L"false", m_maxTempMemSizeInSamples);
            fstream << wstring(str);
        }

        void SetmMaxTempMemSizeInSamples(const size_t maxTempMemSizeInSamples)
        {
            m_maxTempMemSizeInSamples = maxTempMemSizeInSamples;
        }

    private:
        static void WINAPI ComputeInputPartialOverWeight(const ConvolutionNode<ElemType>* pConv, Matrix<ElemType> &gradientValues, 
            Matrix<ElemType> &inputGradientValues, const Matrix<ElemType> &input0, const Matrix<ElemType> &input1, Matrix<ElemType> &tempMatrix, const bool inLoop=false)
        {
            ConvolutionParams convParam = pConv->GetConvolutionParams();

            size_t packedInputRows = convParam.kernelWidth * convParam.kernelHeight * convParam.inputChannels;
            size_t packedInputColsPerSample = convParam.outputWidth * convParam.outputHeight;
            size_t outputSizePerChannel = packedInputColsPerSample;
            size_t packedInputDim = packedInputRows * packedInputColsPerSample; // size of each packed input sample
            size_t inputDim = convParam.inputWidth * convParam.inputHeight * convParam.inputChannels;  //size of each input sample

            long batchSize = (long) input1.GetNumCols(); //right child is the input sample

            long maxTempMemSizeInSamples = (long) (convParam.maxTempMemSizeInSamples == 0? batchSize : convParam.maxTempMemSizeInSamples);

            const Matrix<ElemType> & weightMatrix = input0;
            //inputGradientValues.Resize(weightMatrix.GetNumRows(), weightMatrix.GetNumCols()); //should have been resized when preparing gradient computation

            gradientValues.Reshape(convParam.outputChannels,  outputSizePerChannel * batchSize);  //reshape to match the longernal operation

            long subBatchSize = min(batchSize, maxTempMemSizeInSamples); 
            long numSubBatches = (batchSize+subBatchSize-1)/subBatchSize; 

            if (numSubBatches == 1 && !inLoop)  //reuse packed input from evaluation step if it's not changed by either subbatch or recurrent steps.
            {
                Matrix<ElemType>::MultiplyAndAdd(gradientValues, false, tempMatrix, true, inputGradientValues);
            }
            else
            {
                for (long i=0; i<numSubBatches; i++) 
                {
                    long startSampleID = i*subBatchSize; 
                    long endSampleID = min(batchSize, startSampleID + subBatchSize); 
                    long smallBatchSize = endSampleID-startSampleID; 

                    tempMatrix.Resize(packedInputRows, packedInputColsPerSample * smallBatchSize);
                    Matrix<ElemType> inputSubBatch = input1.ColumnSlice(startSampleID, smallBatchSize);
                    tempMatrix.AssignPackedConvolutionInput(inputSubBatch, 
                                                                     convParam.inputWidth, convParam.inputHeight, convParam.inputChannels,
                                                                     convParam.outputWidth, convParam.outputHeight, convParam.outputChannels,
                                                                     convParam.kernelWidth, convParam.kernelHeight, convParam.horizontalSubsample, convParam.verticalSubsample, 
                                                                     convParam.zeroPadding); 

                    Matrix<ElemType> outputGradientSubBatch = gradientValues.ColumnSlice(startSampleID * outputSizePerChannel, smallBatchSize * outputSizePerChannel);
                    Matrix<ElemType>::MultiplyAndAdd(outputGradientSubBatch, false, tempMatrix, true, inputGradientValues);
                }
            }

            gradientValues.Reshape(convParam.outputChannels * outputSizePerChannel, batchSize);  //change back
        }

        //compute gradient over the packed input and then convert the result to the original input
        static void WINAPI ComputeInputPartialOverInputFeature(const ConvolutionNode<ElemType>* pConv, Matrix<ElemType> &gradientValues, const Matrix<ElemType> &inputGradientValues, const Matrix<ElemType> &input0, const Matrix<ElemType> &input1, Matrix<ElemType> &tempMatrix)
        {
            
            ConvolutionParams convParam = pConv->GetConvolutionParams();
            size_t packedInputRows = convParam.kernelWidth * convParam.kernelHeight * convParam.inputChannels;
            size_t packedInputColsPerSample = convParam.outputWidth * convParam.outputHeight;
            size_t outputSizePerChannel = packedInputColsPerSample;
            size_t packedInputDim = packedInputRows * packedInputColsPerSample; // size of each packed input sample
            size_t inputDim = convParam.inputWidth * convParam.inputHeight * convParam.inputChannels;  //size of each input sample

            long batchSize = (long) input1.GetNumCols(); //right child is the input sample

            long maxTempMemSizeInSamples = (long) (convParam.maxTempMemSizeInSamples == 0? batchSize : convParam.maxTempMemSizeInSamples);

            const Matrix<ElemType> & weightMatrix = input0;

            gradientValues.Reshape(convParam.outputChannels,  outputSizePerChannel * batchSize);  //reshape to match the longernal operation

            long subBatchSize = min(batchSize, maxTempMemSizeInSamples); 
            long numSubBatches = (batchSize+subBatchSize-1)/subBatchSize; 

            for (long i=0; i<numSubBatches; i++) 
            {
                long startSampleID = i*subBatchSize; 
                long endSampleID = min(batchSize, startSampleID + subBatchSize); 
                long smallBatchSize = endSampleID-startSampleID; 

                tempMatrix.Resize(packedInputRows, packedInputColsPerSample * smallBatchSize);
                Matrix<ElemType> outputGradientSubBatch = gradientValues.ColumnSlice(startSampleID * outputSizePerChannel, smallBatchSize * outputSizePerChannel);
                Matrix<ElemType>::Multiply(weightMatrix, true, outputGradientSubBatch, false,  tempMatrix);

                Matrix<ElemType> inputGradientSubBatch = inputGradientValues.ColumnSlice(startSampleID, smallBatchSize);
                tempMatrix.UnpackConvolutionInput(inputGradientSubBatch, 
                                                                 convParam.inputWidth, convParam.inputHeight, convParam.inputChannels,
                                                                 convParam.outputWidth, convParam.outputHeight, convParam.outputChannels,
                                                                 convParam.kernelWidth, convParam.kernelHeight, convParam.horizontalSubsample, convParam.verticalSubsample, 
                                                                 convParam.zeroPadding); 
            }

            gradientValues.Reshape(convParam.outputChannels * outputSizePerChannel, batchSize);  //change back
        }
        

    private:
        size_t m_kernelWidth, m_kernelHeight;
        size_t m_horizontalSubsample, m_verticalSubsample;
        bool m_zeroPadding;

        Matrix<ElemType> m_tempMatrix; 
        size_t m_maxTempMemSizeInSamples; // can change during runtime
    };

    template class ConvolutionNode<float>; 
    template class ConvolutionNode<double>;

    struct PoolParams
    {
        size_t inputWidth, inputHeight, inputChannels;
        size_t windowWidth, windowHeight;
        size_t horizontalSubsample, verticalSubsample;
        size_t outputWidth, outputHeight, outputChannels;
        size_t inputSizePerSample, outputSizePerSample;
    };

    //Max Pooling: support multi channel
    //assume each column is an input sample. Each sample is stored in  (r00, g00, b00, r01, g01, b01, r10, g10, b10, r11, g11, b11)
    template<class ElemType>
    class MaxPoolingNode : public ComputationNode<ElemType>
    {
        typedef ComputationNode<ElemType>* ComputationNodePtr; 

    public:
        MaxPoolingNode( const size_t windowWidth, const size_t windowHeight, 
                        const size_t horizontalSubsample, const size_t verticalSubsample, 
                        const short deviceId=AUTOPLACEMATRIX, const std::wstring name = L"") : ComputationNode(deviceId),
                          m_windowWidth(windowWidth), m_windowHeight(windowHeight),
                          m_horizontalSubsample(horizontalSubsample), m_verticalSubsample(verticalSubsample)                       
        {
            m_nodeName = (name == L""? CreateUniqNodeName() : name);
            m_deviceId = deviceId;
            MoveMatricesToDevice(deviceId);
            InitRecurrentNode();
        }
                
        MaxPoolingNode(File& fstream, const size_t modelVersion, const short deviceId=AUTOPLACEMATRIX, const std::wstring name = L"") : ComputationNode(deviceId)
        {
            m_nodeName = (name == L""? CreateUniqNodeName() : name);
            LoadFromFile(fstream, modelVersion, deviceId);
        }
                
        virtual void SaveToFile(File& fstream) const
        {
            ComputationNode<ElemType>::SaveToFile(fstream);

            fstream << m_windowWidth << m_windowHeight << m_horizontalSubsample << m_verticalSubsample; 
        }

        virtual void LoadFromFile(File& fstream, const size_t modelVersion, const short deviceId=AUTOPLACEMATRIX)
        {
            ComputationNode<ElemType>::LoadFromFile(fstream, modelVersion, deviceId);

            fstream >> m_windowWidth >> m_windowHeight >> m_horizontalSubsample >> m_verticalSubsample; 
        }

        virtual void CopyTo(const ComputationNodePtr nodeP, const std::wstring& newName, const CopyNodeFlags flags) const
        {
            ComputationNode<ElemType>::CopyTo(nodeP, newName, flags);
            MaxPoolingNode<ElemType>* node = (MaxPoolingNode<ElemType>*) nodeP;

            if (flags & CopyNodeFlags::copyNodeValue)
            {
                node->m_inputWidth = m_inputWidth;
                node->m_inputHeight = m_inputHeight;
                node->m_inputChannels = m_inputChannels;

                node->m_windowWidth = m_windowWidth;
                node->m_windowHeight = m_windowHeight;

                node->m_horizontalSubsample = m_horizontalSubsample;
                node->m_verticalSubsample = m_verticalSubsample;

                node->m_outputWidth = m_outputWidth;
                node->m_outputHeight = m_outputHeight;
                node->m_outputChannels = m_outputChannels;

                node->m_inputSizePerSample = m_inputSizePerSample;
                node->m_outputSizePerSample = m_outputSizePerSample;
            }
        }
        // copy constructor
        MaxPoolingNode(const MaxPoolingNode<ElemType>* node, const std::wstring& newName, const CopyNodeFlags flags) : ComputationNode(node->m_deviceId)
        {
            node->CopyTo(this, newName, flags);
        }

        virtual ComputationNodePtr Duplicate(const std::wstring& newName, const CopyNodeFlags flags) const
        {
            const std::wstring& name = (newName == L"")?NodeName():newName;
                
            ComputationNodePtr node = new MaxPoolingNode<ElemType>(this, name, flags);
            return node;
        }

        virtual const std::wstring OperationName() const {return TypeName();}
        static const std::wstring TypeName() {return L"MaxPooling";}

        PoolParams GetPoolParams() const
        {
            PoolParams poolParams;
            poolParams.inputWidth = m_inputWidth;
            poolParams.inputHeight = m_inputHeight;
            poolParams.inputChannels = m_inputChannels;

            poolParams.windowWidth = m_windowWidth;
            poolParams.windowHeight = m_windowHeight;

            poolParams.horizontalSubsample = m_horizontalSubsample;
            poolParams.verticalSubsample = m_verticalSubsample;

            poolParams.outputWidth = m_outputWidth;
            poolParams.outputHeight = m_outputHeight;
            poolParams.outputChannels = m_outputChannels;

            poolParams.inputSizePerSample = m_inputSizePerSample;
            poolParams.outputSizePerSample = m_outputSizePerSample;
            return poolParams;
        }

        virtual void ComputeInputPartial(const size_t inputIndex)
        {
            if (inputIndex > 0)
                throw std::invalid_argument("MaxPooling operation only takes one inputs.");

            ComputeInputPartialS(this, GradientValues(), Inputs(0)->GradientValues(), Inputs(0)->FunctionValues(), FunctionValues());
        }

        virtual void ComputeInputPartial(const size_t inputIndex, const size_t timeIdxInSeq) 
        {
            if (inputIndex > 0)
                throw std::invalid_argument("MaxPooling operation only takes one inputs.");

            Matrix<ElemType> sliceInput0Grad = Inputs(0)->GradientValues().ColumnSlice(timeIdxInSeq * m_samplesInRecurrentStep, m_samplesInRecurrentStep);
            Matrix<ElemType> sliceOutputGrad = GradientValues().ColumnSlice(timeIdxInSeq * m_samplesInRecurrentStep, m_samplesInRecurrentStep);

            Matrix<ElemType> sliceInput0Value = Inputs(0)->FunctionValues().ColumnSlice(timeIdxInSeq * m_samplesInRecurrentStep, m_samplesInRecurrentStep);
            Matrix<ElemType> sliceOutputValue = m_functionValues.ColumnSlice(timeIdxInSeq * m_samplesInRecurrentStep, m_samplesInRecurrentStep);

            ComputeInputPartialS(this, sliceOutputGrad, sliceInput0Grad, sliceInput0Value, sliceOutputValue);
        }

        static void WINAPI ComputeInputPartialS(const MaxPoolingNode<ElemType>* ppool, const Matrix<ElemType> &gradientValues, Matrix<ElemType> &inputGradientValues, const Matrix<ElemType> &input0, const Matrix<ElemType> &functionValues)
        {
            PoolParams poolParams = ppool->GetPoolParams();

            inputGradientValues.AddMaxPoolingGradient(gradientValues, input0, functionValues, poolParams.inputChannels,
                                                    poolParams.inputWidth, poolParams.inputHeight, poolParams.inputSizePerSample, 
                                                    poolParams.outputWidth, poolParams.outputHeight, poolParams.outputSizePerSample, 
                                                    poolParams.windowWidth, poolParams.windowHeight, poolParams.horizontalSubsample, poolParams.verticalSubsample);
        }

        // GetTaskDescriptor - Get a task descriptor for this node
        // taskType - task type we are generating a task for
        virtual TaskDescriptor<ElemType>* GetPTaskDescriptor(TaskType taskType, size_t inputIndex=0) const
        {
            TaskDescriptor<ElemType>* descriptor = new TaskDescriptor<ElemType>(this, taskType, inputIndex);
            switch(taskType)
            {
            case taskComputeInputPartial:
                descriptor->Param(paramTypeNode, "MaxPoolNodePointer", paramOptionsInput | paramOptionsConstant);
                descriptor->GradientParam();
                descriptor->GradientParam(0, paramOptionsInput | paramOptionsOutput | paramOptionsInitialize);
                descriptor->FunctionParam(0, paramOptionsInput);
                descriptor->FunctionParam(-1, paramOptionsInput);
                descriptor->SetFunction((FARPROC)ComputeInputPartialS);
                break;
            case taskEvaluate:
                descriptor->Param(paramTypeNode, "MaxPoolNodePointer", paramOptionsInput | paramOptionsConstant);
                descriptor->FunctionParam();
                descriptor->FunctionParam(0, paramOptionsInput);
                descriptor->SetFunction((FARPROC)EvaluateThisNodeS);
                break;
            default:
                assert(false);
                throw std::logic_error("Unsupported task requested");
            }
            return descriptor;
        }

        virtual void EvaluateThisNode()  
        {
#if NANCHECK
            Inputs(0)->FunctionValues().HasNan("MaxPooling-input0");
#endif
            EvaluateThisNodeS(this, FunctionValues(), Inputs(0)->FunctionValues());
#if NANCHECK
            m_functionValues.HasNan("MaxPooling");
#endif
        }

        virtual void EvaluateThisNode(const size_t timeIdxInSeq) 
        {
            Matrix<ElemType> sliceInput0Value = Inputs(0)->FunctionValues().ColumnSlice(timeIdxInSeq * m_samplesInRecurrentStep, m_samplesInRecurrentStep);
            Matrix<ElemType> sliceOutputValue = m_functionValues.ColumnSlice(timeIdxInSeq * m_samplesInRecurrentStep, m_samplesInRecurrentStep);

            EvaluateThisNodeS(this, sliceOutputValue, sliceInput0Value);
        }

        static void WINAPI EvaluateThisNodeS(const MaxPoolingNode<ElemType>* ppool, Matrix<ElemType> &functionValues, const Matrix<ElemType> &input0)
        {
            PoolParams poolParams = ppool->GetPoolParams();
            functionValues.AssignMaxPoolingResult(input0, poolParams.inputChannels,
                                                 poolParams.inputWidth, poolParams.inputHeight, poolParams.inputSizePerSample, 
                                                 poolParams.outputWidth, poolParams.outputHeight, poolParams.outputSizePerSample, 
                                                 poolParams.windowWidth, poolParams.windowHeight, poolParams.horizontalSubsample, poolParams.verticalSubsample);
        }

        virtual void Validate()
        {
            PrintSelfBeforeValidation();

            if (m_children.size() != 1) 
                throw std::logic_error("MaxPoolingNode requires one input.");

            if (m_horizontalSubsample > m_windowWidth || m_verticalSubsample > m_windowHeight)
                throw std::invalid_argument("MaxPoolingNode: horizontalSubsample must <= windowWidth and verticalSubsample must <= windowHeight.");

            CopyImageSizeFromInputs();

            m_inputSizePerSample = m_inputWidth * m_inputHeight * m_inputChannels;
            m_outputSizePerSample = m_outputWidth * m_outputHeight * m_outputChannels;

            if (Inputs(0)->OperationName() == LearnableParameter<ElemType>::TypeName() && Inputs(0)->FunctionValues().GetNumRows() == 0)
            {
                Inputs(0)->FunctionValues().Resize(m_inputSizePerSample, Inputs(0)->FunctionValues().GetNumCols());
            }

            if (m_children[0]->FunctionValues().GetNumRows() != m_inputSizePerSample)
            {
                msra::strfun::strprintf msg("each column of input to the MaxPooling node %ws is a sample and should have dimension %d, which is inputWidth * inputHeight * inputChannels", 
                    NodeName().c_str(), m_inputSizePerSample);
                throw std::logic_error(msg.c_str());            
            }
            
            if (Inputs(0)->FunctionValues().GetNumElements() == 0)
                throw std::logic_error("MaxPoolingNode operation: the input node has 0 element.");

            m_functionValues.Resize(m_outputSizePerSample, m_children[0]->FunctionValues().GetNumCols());
        }

        virtual void CopyImageSizeFromInputs()
        {
            CopyImageSizeFromInput(0, false);

            if (m_inputWidth < m_windowWidth || m_inputHeight < m_windowHeight)
                throw std::invalid_argument("MaxPoolingNode: inputWidth must >= windowWidth and inputHeight must >= windowHeight.");

            m_outputWidth = (m_inputWidth-m_windowWidth)/m_horizontalSubsample + 1;
            m_outputHeight = (m_inputHeight-m_windowHeight)/m_verticalSubsample + 1;
            m_outputChannels = m_inputChannels;
        }

        virtual void AttachInputs(const ComputationNodePtr inputFeature) 
        {
            m_children.resize(1);
            m_children[0] = inputFeature;
        }

        virtual void DumpNodeInfo(const bool printValues, File& fstream) const
        {
            ComputationNode<ElemType>::DumpNodeInfo(printValues, fstream);

            WCHAR str[4096];
            wsprintf(str, L"Input[Width:%lu, Height:%lu, Channels:%lu]  \n", m_inputWidth, m_inputHeight, m_inputChannels);
            fstream << wstring(str);
            wsprintf(str, L"PoolingWindow[Width:%lu, Height:%lu]  SubSampling[Horizontal:%lu, Vertical:%lu]\n", m_windowWidth, m_windowHeight, m_horizontalSubsample, m_verticalSubsample);
            fstream << wstring(str);
            wsprintf(str, L"Output[Width:%lu, Height:%lu, Channels:%lu]  \n", m_outputWidth, m_outputHeight, m_outputChannels);
            fstream << wstring(str);
            wsprintf(str, L"TotalSizePerSample[Input:%lu, Output:%lu]  \n", m_inputSizePerSample, m_outputSizePerSample);
            fstream << wstring(str);
        }

    private:
        size_t m_windowWidth, m_windowHeight;
        size_t m_horizontalSubsample, m_verticalSubsample;
        size_t m_inputSizePerSample, m_outputSizePerSample;
    };

    template class MaxPoolingNode<float>; 
    template class MaxPoolingNode<double>;    

    //Average Pooling: support multi channel
    //assume each column is an input sample. Each sample is stored in  (r00, g00, b00, r01, g01, b01, r10, g10, b10, r11, g11, b11)
    template<class ElemType>
    class AveragePoolingNode : public ComputationNode<ElemType>
    {
        typedef ComputationNode<ElemType>* ComputationNodePtr; 

    public:
        AveragePoolingNode(const size_t windowWidth, const size_t windowHeight, 
                        const size_t horizontalSubsample, const size_t verticalSubsample, 
                        const short deviceId=AUTOPLACEMATRIX, const std::wstring name = L"") : ComputationNode(deviceId),
                          m_windowWidth(windowWidth), m_windowHeight(windowHeight),
                          m_horizontalSubsample(horizontalSubsample), m_verticalSubsample(verticalSubsample)                     
        {
            m_nodeName = (name == L""? CreateUniqNodeName() : name);
            m_deviceId = deviceId;
            MoveMatricesToDevice(deviceId);
            InitRecurrentNode();
        }

        AveragePoolingNode(File& fstream, const size_t modelVersion, const short deviceId=AUTOPLACEMATRIX, const std::wstring name = L"") : ComputationNode(deviceId)
        {
            m_nodeName = (name == L""? CreateUniqNodeName() : name);
            LoadFromFile(fstream, modelVersion, deviceId);
        }                
                
        virtual void SaveToFile(File& fstream) const
        {
            ComputationNode<ElemType>::SaveToFile(fstream);

            fstream << m_windowWidth << m_windowHeight << m_horizontalSubsample << m_verticalSubsample; 
        }

        virtual void LoadFromFile(File& fstream, const size_t modelVersion, const short deviceId=AUTOPLACEMATRIX)
        {
            ComputationNode<ElemType>::LoadFromFile(fstream, modelVersion, deviceId);

            fstream >> m_windowWidth >> m_windowHeight >> m_horizontalSubsample >> m_verticalSubsample; 
        }

        virtual void CopyTo(const ComputationNodePtr nodeP, const std::wstring& newName, const CopyNodeFlags flags) const
        {
            ComputationNode<ElemType>::CopyTo(nodeP, newName, flags);
            AveragePoolingNode<ElemType>* node = (AveragePoolingNode<ElemType>*) nodeP;

            if (flags & CopyNodeFlags::copyNodeValue)
            {
                node->m_inputWidth = m_inputWidth;
                node->m_inputHeight = m_inputHeight;
                node->m_inputChannels = m_inputChannels;

                node->m_windowWidth = m_windowWidth;
                node->m_windowHeight = m_windowHeight;

                node->m_horizontalSubsample = m_horizontalSubsample;
                node->m_verticalSubsample = m_verticalSubsample;

                node->m_outputWidth = m_outputWidth;
                node->m_outputHeight = m_outputHeight;
                node->m_outputChannels = m_outputChannels;

                node->m_inputSizePerSample = m_inputSizePerSample;
                node->m_outputSizePerSample = m_outputSizePerSample;
            }
        }

        // copy constructor
        AveragePoolingNode(const AveragePoolingNode<ElemType>* node, const std::wstring& newName, const CopyNodeFlags flags) : ComputationNode(node->m_deviceId)
        {
            node->CopyTo(this, newName, flags);
        }

        virtual ComputationNodePtr Duplicate(const std::wstring& newName, const CopyNodeFlags flags) const
        {
            const std::wstring& name = (newName == L"")?NodeName():newName;
                
            ComputationNodePtr node = new AveragePoolingNode<ElemType>(this, name, flags);
            return node;
        }

        virtual const std::wstring OperationName() const {return TypeName();}
        static const std::wstring TypeName() {return L"AveragePooling";}
        PoolParams GetPoolParams() const
        {
            PoolParams poolParams;
            poolParams.inputWidth = m_inputWidth;
            poolParams.inputHeight = m_inputHeight;
            poolParams.inputChannels = m_inputChannels;

            poolParams.windowWidth = m_windowWidth;
            poolParams.windowHeight = m_windowHeight;

            poolParams.horizontalSubsample = m_horizontalSubsample;
            poolParams.verticalSubsample = m_verticalSubsample;

            poolParams.outputWidth = m_outputWidth;
            poolParams.outputHeight = m_outputHeight;
            poolParams.outputChannels = m_outputChannels;

            poolParams.inputSizePerSample = m_inputSizePerSample;
            poolParams.outputSizePerSample = m_outputSizePerSample;
            return poolParams;
        }

        virtual void ComputeInputPartial(const size_t inputIndex)
        {
            if (inputIndex > 0)
                throw std::invalid_argument("AveragePooling operation only takes one inputs.");

            ComputeInputPartialS(this, GradientValues(), Inputs(0)->GradientValues());
        }

        virtual void ComputeInputPartial(const size_t inputIndex, const size_t timeIdxInSeq) 
        {
            if (inputIndex > 0)
                throw std::invalid_argument("AveragePooling operation only takes one inputs.");

            Matrix<ElemType> sliceInput0Grad = Inputs(0)->GradientValues().ColumnSlice(timeIdxInSeq * m_samplesInRecurrentStep, m_samplesInRecurrentStep);
            Matrix<ElemType> sliceOutputGrad = GradientValues().ColumnSlice(timeIdxInSeq * m_samplesInRecurrentStep, m_samplesInRecurrentStep);
            ComputeInputPartialS(this, sliceOutputGrad, sliceInput0Grad);
        }

        static void WINAPI ComputeInputPartialS(const AveragePoolingNode<ElemType>* ppool, const Matrix<ElemType> &gradientValues, Matrix<ElemType> &inputGradientValues)
        {
            PoolParams poolParams = ppool->GetPoolParams();

            inputGradientValues.AddAveragePoolingGradient(gradientValues, poolParams.inputChannels,
                                                    poolParams.inputWidth, poolParams.inputHeight, poolParams.inputSizePerSample, 
                                                    poolParams.outputWidth, poolParams.outputHeight, poolParams.outputSizePerSample, 
                                                    poolParams.windowWidth, poolParams.windowHeight, poolParams.horizontalSubsample, poolParams.verticalSubsample);
        }

                // GetTaskDescriptor - Get a task descriptor for this node
        // taskType - task type we are generating a task for
        virtual TaskDescriptor<ElemType>* GetPTaskDescriptor(TaskType taskType, size_t inputIndex=0) const
        {
            TaskDescriptor<ElemType>* descriptor = new TaskDescriptor<ElemType>(this, taskType, inputIndex);
            switch(taskType)
            {
            case taskComputeInputPartial:
                descriptor->Param(paramTypeNode, "AveragePoolNodePointer", paramOptionsInput | paramOptionsConstant);
                descriptor->GradientParam();
                descriptor->GradientParam(0, paramOptionsInput | paramOptionsOutput | paramOptionsInitialize);
                descriptor->SetFunction((FARPROC)ComputeInputPartialS);
                break;
            case taskEvaluate:
                descriptor->Param(paramTypeNode, "AveragePoolNodePointer", paramOptionsInput | paramOptionsConstant);
                descriptor->FunctionParam();
                descriptor->FunctionParam(0, paramOptionsInput);
                descriptor->SetFunction((FARPROC)EvaluateThisNodeS);
                break;
            default:
                assert(false);
                throw std::logic_error("Unsupported task requested");
            }
            return descriptor;
        }

        virtual void EvaluateThisNode()  
        {
#if NANCHECK
            Inputs(0)->FunctionValues().HasNan("AveragePooling-input0");
#endif
            EvaluateThisNodeS(this, FunctionValues(), Inputs(0)->FunctionValues());
#if NANCHECK
            m_functionValues.HasNan("AveragePooling");
#endif
        }

        virtual void EvaluateThisNode(const size_t timeIdxInSeq) 
        {
            Matrix<ElemType> sliceInput0Value = Inputs(0)->FunctionValues().ColumnSlice(timeIdxInSeq * m_samplesInRecurrentStep, m_samplesInRecurrentStep);
            Matrix<ElemType> sliceOutputValue = m_functionValues.ColumnSlice(timeIdxInSeq * m_samplesInRecurrentStep, m_samplesInRecurrentStep);

            EvaluateThisNodeS(this, sliceOutputValue, sliceInput0Value);
        }

        static void WINAPI EvaluateThisNodeS(const AveragePoolingNode<ElemType>* ppool, Matrix<ElemType> &functionValues, const Matrix<ElemType> &input0)
        {
            PoolParams poolParams = ppool->GetPoolParams();
            
            functionValues.AssignAveragePoolingResult(input0, poolParams.inputChannels,
                                                 poolParams.inputWidth, poolParams.inputHeight, poolParams.inputSizePerSample, 
                                                 poolParams.outputWidth, poolParams.outputHeight, poolParams.outputSizePerSample, 
                                                 poolParams.windowWidth, poolParams.windowHeight, poolParams.horizontalSubsample, poolParams.verticalSubsample);
        }

        virtual void Validate()
        {
            PrintSelfBeforeValidation();

            if (m_children.size() != 1) 
                throw std::logic_error("AveragePoolingNode requires one input.");

            if (m_horizontalSubsample > m_windowWidth || m_verticalSubsample > m_windowHeight)
                throw std::invalid_argument("AveragePoolingNode: horizontalSubsample must <= windowWidth and verticalSubsample must <= windowHeight.");

            CopyImageSizeFromInputs();

            m_inputSizePerSample = m_inputWidth * m_inputHeight * m_inputChannels;
            m_outputSizePerSample = m_outputWidth * m_outputHeight * m_outputChannels;

            if (Inputs(0)->OperationName() == LearnableParameter<ElemType>::TypeName() && Inputs(0)->FunctionValues().GetNumRows() == 0)
            {
                Inputs(0)->FunctionValues().Resize(m_inputSizePerSample, Inputs(0)->FunctionValues().GetNumCols());
            }

            if (m_children[0]->FunctionValues().GetNumRows() != m_inputSizePerSample)
            {
                msra::strfun::strprintf msg("each column of input to the AveragePooling node %ws is a sample and should have dimension %d, which is inputWidth * inputHeight * inputChannels", 
                    NodeName().c_str(), m_inputSizePerSample);
                throw std::logic_error(msg.c_str());            
            }
                        
            if (Inputs(0)->FunctionValues().GetNumElements() == 0)
                throw std::logic_error("AveragePoolingNode operation: the input node has 0 element.");

            FunctionValues().Resize(m_outputSizePerSample, m_children[0]->FunctionValues().GetNumCols());
        }

        virtual void CopyImageSizeFromInputs()
        {
            CopyImageSizeFromInput(0, false);

            if (m_inputWidth < m_windowWidth || m_inputHeight < m_windowHeight)
                throw std::invalid_argument("AveragePoolingNode: inputWidth must >= windowWidth and inputHeight must >= windowHeight.");

            m_outputWidth = (m_inputWidth-m_windowWidth)/m_horizontalSubsample + 1;
            m_outputHeight = (m_inputHeight-m_windowHeight)/m_verticalSubsample + 1;
            m_outputChannels = m_inputChannels;
        }

        virtual void AttachInputs(const ComputationNodePtr inputFeature) 
        {
            m_children.resize(1);
            m_children[0] = inputFeature;
        }

        virtual void DumpNodeInfo(const bool printValues, File& fstream) const
        {
            ComputationNode<ElemType>::DumpNodeInfo(printValues, fstream);

            WCHAR str[4096];
            wsprintf(str, L"Input[Width:%lu, Height:%lu, Channels:%lu]  \n", m_inputWidth, m_inputHeight, m_inputChannels);
            fstream << wstring(str);
            wsprintf(str, L"PoolingWindow[Width:%lu, Height:%lu]  SubSample[Horizontal:%lu, Vertical:%lu]\n", m_windowWidth, m_windowHeight, m_horizontalSubsample, m_verticalSubsample);
            fstream << wstring(str);
            wsprintf(str, L"Output[Width:%lu, Height:%lu, Channels:%lu]  \n", m_outputWidth, m_outputHeight, m_outputChannels);
            fstream << wstring(str);
            wsprintf(str, L"TotalSizePerSample[Input:%lu, Output:%lu]  SubSample[Horizontal:%lu, Vertical:%lu]\n", m_inputSizePerSample, m_outputSizePerSample);
            fstream << wstring(str);
        }

    private:
        size_t m_windowWidth, m_windowHeight;
        size_t m_horizontalSubsample, m_verticalSubsample;
        size_t m_inputSizePerSample, m_outputSizePerSample;
    };

    template class AveragePoolingNode<float>; 
    template class AveragePoolingNode<double>;    
}}}