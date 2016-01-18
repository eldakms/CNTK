//
// <copyright file="ModelEditLanguage.cpp" company="Microsoft">
//     Copyright (c) Microsoft Corporation.  All rights reserved.
// </copyright>
//
// NetworkDescriptionLanguage.cpp : Code used to interpret the Network Description Language.
//
#include "ModelEditLanguage.h"
#include <map>

namespace Microsoft { namespace MSR { namespace CNTK {

// EqualInsensitive - check to see if two nodes are equal up to the length of the first string (must be at least half as long as actual node name)
// string1 - [in,out] string to compare, if comparision is equal insensitive but not sensitive, will replace with sensitive version
// string2 - second string to compare
// alternate - alternate naming of the string
// return - true if strings are equal insensitive and modifies string1 to sensitive version if different
bool EqualInsensitive(std::string& string1, const char* string2, const char* alternate/*=NULL*/)
{
    bool equal = !_strnicmp(string1.c_str(), string2, string1.size());

    // don't allow partial matches that are less than half the string
    if (equal && string1.size() < strlen(string2)/2)
    {
        equal = false;
    }

    // if we have a (partial) match replace with the full name
    if (equal && strcmp(string1.c_str(), string2))
    {
        string1 = string2;
    }

    if (!equal && alternate != NULL)
    {
        equal = !_strnicmp(string1.c_str(), alternate, string1.size());

        // don't allow partial matches that are less than half the string
        if (equal && string1.size() < strlen(alternate)/2)
        {
            equal = false;
        }

        // if we have a match of the alternate string replace with the full name
        if (equal)
        {
            string1 = string2;
        }
    }

    return equal;
}

// MELProperty - the properties for SetProperty
enum MELProperty
{
    melPropNull,
    melPropComputeGradient,
    melPropFeature,
    melPropLabel,
    melPropFinalCriterion,
    melPropEvaluation,
    melPropOutput,
    melPropRecurrent
};

// SetProperty - Set the Property on the passed node
// nodeProp - node on which the property will be set/cleared
// propArray - Array which contains all nodes that are associated with a particular property
// set - true if property is to be added, false if property is deleted
template <typename ElemType>
void MELScript<ElemType>::SetProperty(ComputationNode<ElemType>* nodeProp, vector<ComputationNode<ElemType>*>& propArray, bool set)
{
    vector<ComputationNode<ElemType>*>::iterator found = propArray.begin();
    for (;found != propArray.end() && *found != nodeProp; ++found)
        ; // loop until you find the node, or the end

    if (set && found == propArray.end())
    {
        propArray.push_back(nodeProp);
    }
    else if (!set && found != propArray.end())
    {
        propArray.erase(found);
    }
}

// ProcessNDLScript - Process the NDL script 
// netNdl - netNDL structure
// ndlPassUntil - complete processing through this pass, all passes if ndlPassAll
// fullValidate - validate as a complete network? (false if this might be a snippet of a full network)
template <typename ElemType>
void MELScript<ElemType>::ProcessNDLScript(NetNdl<ElemType>* netNdl, NDLPass ndlPassUntil=ndlPassAll, bool fullValidate = false)
{
    NDLUtil<ElemType> ndlUtil(netNdl->cn);
    ndlUtil.ProcessNDLScript(netNdl, ndlPassUntil, fullValidate);
}

// CallFunction - call the MEL function
// name - name of the function to call
// params - parameters to the function
template <typename ElemType>
void MELScript<ElemType>::CallFunction(const std::string& p_name, const ConfigParamList& params)
{
    std::string name = p_name;
    bool ret = false;
    if (EqualInsensitive(name, "CreateModel"))  //create a blank model
    {
        size_t numFixedParams = 0, numOptionalParams = 0;
        if (params.size() > numFixedParams + numOptionalParams || params.size() < numFixedParams)
            Error("Invalid number of parameters. Valid parameters: CreateModel(). newly created model always becomes the new default.");

        ComputationNetwork<ElemType>* cn = new ComputationNetwork<ElemType>(CPUDEVICE);
        OverrideModelNameAndSetDefaultModel(cn);
    }
    if (EqualInsensitive(name, "CreateModelWithName"))  //create a blank model
    {
        size_t numFixedParams = 1, numOptionalParams = 0;
        if (params.size() > numFixedParams + numOptionalParams || params.size() < numFixedParams)
            Error("Invalid number of parameters. Valid parameters: CreateModelWithName(modelName). newly created model always becomes the new default.");

        ComputationNetwork<ElemType>* cn = new ComputationNetwork<ElemType>(CPUDEVICE);
        OverrideModelNameAndSetDefaultModel(cn, params[0]);
    }
    else if (EqualInsensitive(name, "LoadModel"))
    {
        size_t numFixedParams = 1, numOptionalParams = 1;
        if (params.size() > numFixedParams + numOptionalParams || params.size() < numFixedParams)
            Error("Invalid number of parameters. Valid parameters: LoadModel(modelFileName, [format=cntk]). newly loaded model always becomes the new default.");

        std::wstring modelFormat = GetOptionalModelFormat(params, numFixedParams);

        ComputationNetwork<ElemType>* cn = new ComputationNetwork<ElemType>(CPUDEVICE);
        cn->LoadFromFile(params[0]);
        OverrideModelNameAndSetDefaultModel(cn);
    }
    else if (EqualInsensitive(name, "LoadModelWithName"))
    {
        size_t numFixedParams = 2, numOptionalParams = 1;
        if (params.size() > numFixedParams + numOptionalParams || params.size() < numFixedParams)
            Error("Invalid number of parameters. Valid parameters: LoadModelWithName(modelName, modelFileName, [format=cntk]). newly loaded model always becomes the new default.");

        std::wstring modelFormat = GetOptionalModelFormat(params, numFixedParams);

        ComputationNetwork<ElemType>* cn = new ComputationNetwork<ElemType>(CPUDEVICE);
        cn->LoadFromFile(params[1]);
        OverrideModelNameAndSetDefaultModel(cn, params[0]);
    }
    else if (EqualInsensitive(name, "LoadNDLSnippet"))
    {
        size_t numFixedParams = 2, numOptionalParams = 1;
        if (params.size() > numFixedParams + numOptionalParams || params.size() < numFixedParams)
            Error("Invalid number of parameters. Valid parameters: LoadNDLSnippet(modelName, ndlsnippet).");

        string modelName = params[0];
        wstring ndlSnippetFileName = params[1];
        ComputationNetwork<ElemType>* cn = new ComputationNetwork<ElemType>(CPUDEVICE);
        NDLScript<ElemType> script;
        ConfigParameters ndlScript = script.ReadConfigFile(ndlSnippetFileName);

        // check for a section of the snippet file we wish to read
        std::string section = GetOptionalSnippetSection(params, numFixedParams);

        if (!section.empty())
        {
            if (!ndlScript.Exists(section))
            {
                Error("Section %s specified in optional parameter was not found in the %ls file\n", section.c_str(), ndlSnippetFileName.c_str());
            }
            ConfigValue ndlSnippet = ndlScript(section);
            EvaluateNDLSnippet(ndlSnippet, cn);
        }
        else
        {
            script.LoadConfigFile(ndlSnippetFileName);
        }

        OverrideModelNameAndSetDefaultModel(cn, modelName);
    }
    else if (EqualInsensitive(name, "SaveDefaultModel"))
    {
        size_t numFixedParams = 1, numOptionalParams = 1;
        if (params.size() > numFixedParams + numOptionalParams || params.size() < numFixedParams)
            Error("Invalid number of parameters. Valid parameters: SaveDefaultModel(modelFileName, [format=cntk]).");

        std::wstring modelFormat = GetOptionalModelFormat(params, numFixedParams);

        std::wstring fileName = params[0];

        ComputationNetwork<ElemType>* cn = m_netNdlDefault->cn;
        if (cn == NULL)
            Error("SaveDefaultModel can only be called after a default name exists (i.e., at least one model is loaded.)");

        // validate the network before we save it out
        ProcessNDLScript(m_netNdlDefault, ndlPassAll, true);

        cn->SaveToFile(fileName);
    }
    else if (EqualInsensitive(name, "SaveModel"))
    {
        size_t numFixedParams = 2, numOptionalParams = 1;
        if (params.size() > numFixedParams + numOptionalParams || params.size() < numFixedParams)
            Error("Invalid number of parameters. Valid parameters: SaveModel(modelName, modelFileName, [format=cntk]).");

        std::wstring modelFormat = GetOptionalModelFormat(params, numFixedParams);

        std::string modelName = params[0];
        std::wstring fileName = params[1];

        NetNdl<ElemType>* netNdl = &m_mapNameToNetNdl[modelName];
        if (netNdl->cn == NULL)
            Error("SaveModel can only be called after a network has been setup, no active model named %ls.", modelName.c_str());

        // validate and finish the second pass through NDL if any in-line NDL was defined
        ProcessNDLScript(netNdl, ndlPassAll, true);
        netNdl->cn->SaveToFile(fileName);
    }
    else if (EqualInsensitive(name, "SetDefaultModel"))
    {
        size_t numFixedParams = 1, numOptionalParams = 0;
        if (params.size() > numFixedParams + numOptionalParams || params.size() < numFixedParams)
            Error("Invalid number of parameters. Valid parameters: SetDefaultModel(modelName)");

        SetExistingModelAsDefault(params[0]);
    }
    else if (EqualInsensitive(name, "UnloadModel"))
    {
        // UnloadModel takes a variable number of parameters, all expected to be model names
        for (int i=0; i < params.size(); ++i)
        {
            string modelName = params[i];
            auto found = m_mapNameToNetNdl.find(modelName);
            if (found != m_mapNameToNetNdl.end())
            {
                found->second.Clear();
                // if this was the default model, clear it out
                if (&(found->second) == m_netNdlDefault)
                    m_netNdlDefault = nullptr;
                m_mapNameToNetNdl.erase(found);
            }
            else
                fprintf(stderr, "WARNING: model %s does not exist.", modelName);
        }
    }
    else if (EqualInsensitive(name, "DumpModel", "Dump"))        
    {
        size_t numFixedParams = 2, numOptionalParams = 1;
        if (params.size() > numFixedParams + numOptionalParams || params.size() < numFixedParams)
            Error("Invalid number of parameters. Valid parameters: DumpNetwork(modelName, fileName, [includeData=false|true])");

        bool includeData = GetOptionalIncludeDataValue(params, numFixedParams);

        std::string modelName = params[0];
        std::wstring fileName = params[1];

        auto found = m_mapNameToNetNdl.find(modelName);
        if (found == m_mapNameToNetNdl.end())
            Error("Model %s does not exist. Cannot dump non-existant model.", modelName);
        else
        {
            NetNdl<ElemType>* netNdl = &found->second;
            ProcessNDLScript(netNdl, ndlPassAll, true);
            found->second.cn->DumpAllNodesToFile(includeData, fileName);
        }
    }    
    else if (EqualInsensitive(name, "DumpNode"))        
    {
        size_t numFixedParams = 2, numOptionalParams = 1;
        if (params.size() > numFixedParams + numOptionalParams || params.size() < numFixedParams)
            Error("Invalid number of parameters. Valid parameters: DumpNode(nodeName, fileName, [includeData=false|true])");

        bool includeData = GetOptionalIncludeDataValue(params, numFixedParams);

        std::wstring fileName = params[1];

        NetNdl<ElemType>* netNdl;
        vector<ComputationNode<ElemType>*> nodes = FindSymbols(params[0], netNdl);
        ProcessNDLScript(netNdl, ndlPassAll);
        netNdl->cn->DumpNodeInfoToFile(nodes, includeData, fileName);
    }
    else if (EqualInsensitive(name, "CopyNode", "Copy"))
    {
        size_t numFixedParams = 2, numOptionalParams = 1;
        if (params.size() > numFixedParams + numOptionalParams || params.size() < numFixedParams)
            Error("Invalid number of parameters. Valid parameters are: CopyNode(fromNode, toNode, [copy=all|value])");

        CopyNodeFlags copyFlags = GetOptionalCopyNodeFlags(params, numFixedParams);

        std::string from = params[0];
        std::string to = params[1];
        CopyNodes(from, to, copyFlags);
    }
    else if (EqualInsensitive(name, "CopySubTree"))
    {
        size_t numFixedParams = 3, numOptionalParams = 1;
        if (params.size() > numFixedParams + numOptionalParams || params.size() < numFixedParams)
            Error("Invalid number of parameters. Valid parameters are: CopySubTree(fromNode, toNetwork, toNodeNamePrefix, [copy=all|value])");

        CopyNodeFlags copyFlags = GetOptionalCopyNodeFlags(params, numFixedParams);

        std::string from = params[0];
        std::string to = params[1];
        std::string prefix = params[2];
        CopySubTree(from, to, prefix, copyFlags);
    }
    else if (EqualInsensitive(name, "CopyNodeInputs", "CopyInputs"))
    {
        size_t numFixedParams = 2, numOptionalParams = 0;
        if (params.size() > numFixedParams + numOptionalParams || params.size() < numFixedParams)
            Error("Invalid number of parameters. Valid parameters are: CopyNodeInputs(fromNode, toNode)");

        // get the nodes
        NetNdl<ElemType>* netNdlTo;
        NetNdl<ElemType>* netNdlFrom;
        vector<GenNameValue> names = GenerateNames(params[0], params[1], netNdlFrom, netNdlTo);

        if (netNdlFrom != netNdlTo)
            Error("CopyInputs requires two symbols from the same network, %s and %s belong to different networks", params[0], params[1]);

        ProcessNDLScript(netNdlFrom, ndlPassAll);
        for each (GenNameValue name in names)
        {
            ComputationNode<ElemType>* node = name.first;
            std::wstring nodeName = node->NodeName();
            std::wstring toNodeName = name.second;

            ComputationNode<ElemType>* nodeOut = netNdlTo->cn->CopyNode(*netNdlFrom->cn, nodeName,toNodeName,CopyNodeFlags::copyNodeChildren);
        }
    }
    else if (EqualInsensitive(name, "SetNodeInput", "SetInput"))
    {
        size_t numFixedParams = 3, numOptionalParams = 0;
        if (params.size() > numFixedParams + numOptionalParams || params.size() < numFixedParams)
            Error("Invalid number of parameters. Valid parameters are: SetNodeInput(toNode, inputID(0-based), inputNodeName)");

        // get the nodes
        NetNdl<ElemType>* netNdlTo;
        NetNdl<ElemType>* netNdlFrom;
        vector<ComputationNode<ElemType>*> nodeTo = FindSymbols(params[0], netNdlTo);
        vector<ComputationNode<ElemType>*> nodeFrom = FindSymbols(params[2], netNdlFrom);
        int inputNum = params[1];

        if (netNdlTo != netNdlFrom)
            Error("SetNodeInput() requires two symbols from the same network, %s and %s belong to different networks", params[0], params[2]);

        if (nodeFrom.size() != 1)
            Error("SetNodeInput() must have a single value input, %s doesn't represent one item",params[0]);
        if (nodeTo.size() < 1)
            Error("SetNodeInput() must have at least one target, %s doesn't represent any items",params[2]);

        // process outstanding NDL scripts ensuring that the inputs have all been resolved
        ProcessNDLScript(netNdlFrom, ndlPassResolve); 
        for each (ComputationNode<ElemType>* node in nodeTo)
        {
            node->SetInput(inputNum, nodeFrom[0]);
        }
    }
    else if (EqualInsensitive(name, "SetNodeInputs", "SetInputs"))
    {
        if (params.size() > 4 || params.size() < 2)
            Error("Invalid number of parameters. Valid parameters are: SetNodeInputs(toNode, inputNodeName1, [inputNodeName2, inputNodeName3])");

        // get the nodes
        NetNdl<ElemType>* netNdlTo;
        vector<ComputationNode<ElemType>*> nodeTo = FindSymbols(params[0], netNdlTo);
        if (nodeTo.size() != 1)
            Error("SetNodeInputs() must have exactly one target, %s doesn't represent any node.",params[0]);
        
        vector<ComputationNode<ElemType>*> inputNodes;
        inputNodes.resize(params.size()-1);

        // process outstanding NDL scripts ensuring that the inputs have all been resolved
        ProcessNDLScript(netNdlTo, ndlPassResolve); 

        for (int i=1; i<params.size(); i++)
        {
            NetNdl<ElemType>* netNdlFrom;
            vector<ComputationNode<ElemType>*> nodeFrom = FindSymbols(params[i], netNdlFrom);

            if (netNdlTo != netNdlFrom)
                Error("SetNodeInputs() requires all symbols from the same network, %s and %s belong to different networks", params[0], params[i]);

            if (nodeFrom.size() != 1)
                Error("SetNodeInputs() each input node should be translated to one node name. %s is translated to multiple node names.",  params[i]);

            inputNodes[i-1] = nodeFrom[0];
        }

        if (inputNodes.size() == 1)
            nodeTo[0]->AttachInputs(inputNodes[0]);
        else if (inputNodes.size() == 2)
            nodeTo[0]->AttachInputs(inputNodes[0], inputNodes[1]);
        else if (inputNodes.size() == 3)
            nodeTo[0]->AttachInputs(inputNodes[0], inputNodes[1], inputNodes[2]);
        else
            Error("SetNodeInputs(): You specified more than 3 input nodes.");
    }
    else if (EqualInsensitive(name, "SetProperty"))
    {
        if (params.size() != 3)
            Error("Invalid number of parameters: Valid parameters are: SetProperty(toNode, propertyName, propertyValue)");
        
        std::string propName = params[1];
        MELProperty prop=melPropNull;
        if (EqualInsensitive(propName, "ComputeGradient", "NeedsGradient"))
        {
            prop = melPropComputeGradient;
        }
        else if (EqualInsensitive(propName, "Feature"))
        {
            prop = melPropFeature;
        }
        else if (EqualInsensitive(propName, "Label"))
        {
            prop = melPropLabel;
        }
        else if (EqualInsensitive(propName, "FinalCriterion", "Criteria"))
        {
            prop = melPropFinalCriterion;
        }
        else if (EqualInsensitive(propName, "Evaluation", "Eval"))
        {
            prop = melPropEvaluation;
        }
        else if (EqualInsensitive(propName, "Output"))
        {
            prop = melPropOutput;
        }
        else if (EqualInsensitive(propName, "Recurrent"))
        {
            prop = melPropRecurrent;
        }
        else
        {
            Error("Invalid property, %s, is not supported", propName);
        }

        // get the nodes
        NetNdl<ElemType>* netNdl;
        vector<ComputationNode<ElemType>*> nodes = FindSymbols(params[0], netNdl);

        // this probabably won't do anything, but make sure all NDL has been created
        ProcessNDLScript(netNdl, ndlPassInitial, false);

        ComputationNetwork<ElemType>* cn = netNdl->cn;
        for each (ComputationNode<ElemType>* node in nodes)
        {
            switch(prop)
            {
                case melPropComputeGradient:
                {
                    node->NeedGradient() = params[2];
                    break;
                }
                case melPropFeature:
                {
                    bool set = params[2];
                    SetProperty(node, cn->FeatureNodes(), set);
                    break;
                }
                case melPropLabel:
                {
                    bool set = params[2];
                    SetProperty(node, cn->LabelNodes(), set);
                    break;
                }
                case melPropFinalCriterion:
                {
                    bool set = params[2];
                    SetProperty(node, cn->FinalCriterionNodes(), set);
                    break;
                }
                case melPropEvaluation:
                {
                    bool set = params[2];
                    SetProperty(node, cn->EvaluationNodes(), set);
                    break;
                }
                case melPropOutput:
                {
                    bool set = params[2];
                    SetProperty(node, cn->OutputNodes(), set);
                    break;
                }
                case melPropRecurrent:
                {
                    // what to do here?
                    break;
                }
                default:
                {
                    Error("Invalid property, %s, is not supported", propName);
                    break;
                }
            }
        }
    }
    else if (EqualInsensitive(name, "SetPropertyForSubTree"))
    {
        size_t numFixedParams = 3, numOptionalParams = 0;
        if (params.size() > numFixedParams + numOptionalParams || params.size() < numFixedParams)
            Error("Invalid number of parameters. Valid parameters are: SetPropertyForSubTree(rootNodeName, propertyName, propertyValue)");
        
        std::string propName = params[1];
        MELProperty prop=melPropNull;
        if (EqualInsensitive(propName, "ComputeGradient", "NeedsGradient"))
        {
            prop = melPropComputeGradient;
        }
        else
        {
            Error("Invalid property, %s, is not supported", propName);
        }

        // get the nodes
        NetNdl<ElemType>* netNdl;
        vector<ComputationNode<ElemType>*> nodes = FindSymbols(params[0], netNdl);

        // make sure all NDL links have been resolved
        ProcessNDLScript(netNdl, ndlPassResolve);

        for each (ComputationNode<ElemType>* node in nodes)
        {
            switch(prop)
            {
                case melPropComputeGradient:
                {
                    bool needGradient = params[2];
                    netNdl->cn->SetLeanableNodesBelowNeedGradient(needGradient, node);
                    break;
                }
                default:
                {
                    Error("Invalid property, %s, is not supported", propName);
                    break;
                }
            }
        }
    }
    else if (EqualInsensitive(name, "RemoveNode", "Remove") || EqualInsensitive(name, "DeleteNode", "Delete"))
    {
        std::map<NetNdl<ElemType>*,bool> processed;
        // remove takes a variable number of parameters, all expected to be node names or wildcard patterns
        for (int i=0; i < params.size(); ++i)
        {
            // get the nodes
            NetNdl<ElemType>* netNdl;
            vector<ComputationNode<ElemType>*> nodes = FindSymbols(params[i], netNdl);

            // make sure all NDL has been processed in case we are removing some of them...
            // only process each network once, because validates will start failing after first delete
            if (processed.find(netNdl) == processed.end())
            {
                ProcessNDLScript(netNdl, ndlPassAll, false);
                processed[netNdl] = true;
            }

            if (nodes.size() < 1)
                Error("Delete must have at least one target, %s doesn't represent any items",params[i]);
            for each (ComputationNode<ElemType>* node in nodes)
            {
                netNdl->cn->DeleteNode(node->NodeName());
            }
        }
    }
    else if (EqualInsensitive(name, "Rename"))
    {
        size_t numFixedParams = 2, numOptionalParams = 0;
        if (params.size() > numFixedParams + numOptionalParams || params.size() < numFixedParams)
            Error("Invalid number of parameters. Valid parameters are Rename(oldNodeName, newNodeName)");

        // get the nodes
        NetNdl<ElemType>* netNdlTo;
        NetNdl<ElemType>* netNdlFrom;
        vector<GenNameValue> nodeNames = GenerateNames(params[0], params[1], netNdlFrom, netNdlTo);

        if (netNdlFrom != netNdlTo)
            Error("CopyInputs requires two symbols from the same network, %s and %s belong to different networks", params[0], params[1]);

        // process everything in case these nodes may have tags on them
        ProcessNDLScript(netNdlFrom, ndlPassAll);

        // now we have the original nodeNames from the input symbol, generate the output nodeNames
        for each (GenNameValue nodeName in nodeNames)
        {
            ComputationNode<ElemType>* node = nodeName.first;
            netNdlFrom->cn->RenameNode(node, nodeName.second);
        }
    }
    else
    {
        Error("Unknown Editor function %s", name.c_str());
    }
}

template class MELScript<float>; 
template class MELScript<double>;

}}}