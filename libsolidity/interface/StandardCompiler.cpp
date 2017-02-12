/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @author Alex Beregsazszi
 * @date 2016
 * Full-stack compiler that converts a source code string to bytecode.
 */

#include <libsolidity/interface/StandardCompiler.h>

#include <libsolidity/interface/CompilerStack.h>
#include <libsolidity/interface/Version.h>
#include <libsolidity/analysis/SemVerHandler.h>
#include <libsolidity/ast/AST.h>
#include <libsolidity/parsing/Scanner.h>
#include <libsolidity/parsing/Parser.h>
#include <libsolidity/analysis/GlobalContext.h>
#include <libsolidity/analysis/NameAndTypeResolver.h>
#include <libsolidity/analysis/TypeChecker.h>
#include <libsolidity/analysis/DocStringAnalyser.h>
#include <libsolidity/analysis/StaticAnalyzer.h>
#include <libsolidity/analysis/SyntaxChecker.h>
#include <libsolidity/codegen/Compiler.h>
#include <libsolidity/interface/InterfaceHandler.h>
#include <libsolidity/formal/Why3Translator.h>

#include <libevmasm/Exceptions.h>

#include <libdevcore/SwarmHash.h>
#include <libdevcore/JSON.h>

#include <json/json.h>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>


using namespace std;
using namespace dev;
using namespace dev::solidity;

string StandardCompiler::compile(string const& _input)
{
	Json::Value input;
	if (!Json::Reader().parse(_input, input, false))
	{
		return "{\"errors\":\"[{\"type\":\"InputError\",\"component\":\"general\",\"severity\":\"error\",\"message\":\"Error parsing input JSON.\"}]}";
	}
//	return jsonCompactPrint(compile(input));
cout << "Input: " << input.toStyledString() << endl;
	Json::Value output = compile(input);
cout << "Output: " << output.toStyledString() << endl;
	return jsonCompactPrint(output);
}

Json::Value StandardCompiler::compile(Json::Value const& _input)
{
	m_compilerStack.reset(false);

	Json::Value const& sources = _input["sources"];
	for (auto const& sourceName: sources.getMemberNames())
		m_compilerStack.addSource(sourceName, sources[sourceName]["content"].asString());

	Json::Value const& settings = _input["settings"];

	vector<string> remappings;
	for (auto const& remapping: settings["remappings"])
		remappings.push_back(remapping.asString());
	m_compilerStack.setRemappings(remappings);

	Json::Value optimizerSettings = settings.get("optimizer", Json::Value());
	bool optimize = optimizerSettings.get("enabled", Json::Value(false)).asBool();
	unsigned optimizeRuns = optimizerSettings.get("runs", Json::Value(200u)).asUInt();

	map<string, h160> libraries;
//	{
//		Json::Value jsonLibraries = settings.get("libraries", Json::Value());
//		for (auto const& library: jsonLibraries.getMemberNames())
//			libraries[library] = h160(jsonLibraries[library].asString());
//	}

	//@TODO document: Can accept wildcards
	//@TODO document: plural
	vector<string> compilationTargets;
	{
		Json::Value targets = settings.get("compilationTargets", Json::Value());
	}

	try {
		m_compilerStack.compile(optimize, optimizeRuns, libraries);
	}
/*
        catch (CompilerError const& _exception)
        {
                SourceReferenceFormatter::printExceptionInformation(cerr, _exception, "Compiler error", scannerFromSourceName);
                return false;
        }
*/
        catch (InternalCompilerError const& _exception)
        {
                cerr << "Internal compiler error during compilation:" << endl
                         << boost::diagnostic_information(_exception);
                return Json::Value();
        }
        catch (UnimplementedFeatureError const& _exception)
        {
                cerr << "Unimplemented feature:" << endl
                         << boost::diagnostic_information(_exception);
                return Json::Value();
        }
/*
        catch (Error const& _error)
        {
                if (_error.type() == Error::Type::DocstringParsingError)
                        cerr << "Documentation parsing error: " << *boost::get_error_info<errinfo_comment>(_error) << endl;
                else
                        SourceReferenceFormatter::printExceptionInformation(cerr, _error, _error.typeName(), scannerFromSourceName);

                return Json::Value();
        }
*/
	catch (Exception const& _exception)
	{
		cerr << "Exception during compilation: " << boost::diagnostic_information(_exception) << endl;
		return Json::Value();
	}
	catch (...)
	{
		cerr << "Unknown exception during compilation." << endl;
		return Json::Value();
	}

	//@TODO unlinked linker files should be output as such
	// What are the use-cases?
	// only type check
	// only extract documentation
	// only extract list of all contracts / libraries

	Json::Value output = Json::objectValue;

	vector<string> contracts = m_compilerStack.contractNames();
        if (!contracts.empty())
        {
		output["contracts"] = Json::objectValue;
		output["contracts"][""] = Json::objectValue;

		Json::Value contractsOutput = Json::objectValue;
		for (string const& contractName: contracts)
		{
			Json::Value contractData(Json::objectValue);
                        contractData["abi"] = dev::jsonCompactPrint(m_compilerStack.interface(contractName));
                        contractData["metadata"] = m_compilerStack.onChainMetadata(contractName);
                        
                        Json::Value evmData(Json::objectValue);
                        evmData["bytecode"] = m_compilerStack.object(contractName).toHex();
                        evmData["runtimebytecode"] = m_compilerStack.runtimeObject(contractName).toHex();
                        evmData["clone"] = m_compilerStack.cloneObject(contractName).toHex();
                        evmData["opcodes"] = solidity::disassemble(m_compilerStack.object(contractName).bytecode);
//                {
//                        ostringstream unused;
//                        contractData["assembly"] = m_compilerStack.streamAssembly(unused, contractName, m_sourceCodes, true);
//                }
                {
                        auto map = m_compilerStack.sourceMapping(contractName);
                        evmData["srcmap"] = map ? *map : "";
                }
                {
                        auto map = m_compilerStack.runtimeSourceMapping(contractName);
                        evmData["srcmapruntime"] = map ? *map : "";
                }
            		contractData["evm"] = evmData;
                        contractData["devdoc"] = dev::jsonCompactPrint(m_compilerStack.metadata(contractName, DocumentationType::NatspecDev));
                        contractData["userdoc"] = dev::jsonCompactPrint(m_compilerStack.metadata(contractName, DocumentationType::NatspecUser));
                
            		contractsOutput[contractName] = contractData;
		}
		output["contracts"][""] = contractsOutput;
        }

	return output;
}
