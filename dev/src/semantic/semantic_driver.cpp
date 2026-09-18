#include "semantic/semantic_driver.h"

#include <fstream>
#include <stdexcept>

#include "semantic/semantic_analyzer.h"
#include "semantic/semantic_model.h"
#include "semantic/semantics_tree.h"
#include "syntax/syntax_driver.h"

using namespace std;

namespace cppgm
{
namespace semantic
{

void EmitSemantics(const vector<string>& sources, const string& outfile)
{
	ofstream out(outfile.c_str());
	if(!out)
	{
		throw runtime_error("cannot open the output file");
	}

	string build_date;
	string build_time;
	syntax::BuildStamp(build_date, build_time);

	out << sources.size() << " translation units\n";

	for(size_t index = 0; index < sources.size(); ++index)
	{
		syntax::ParsedSource parsed;
		syntax::ParseSource(sources[index], build_date, build_time, parsed);

		out << "start translation unit " << (index + 1) << '\n';

		Model model;
		Analyzer analyzer(model, parsed.arena, parsed.spellings, parsed.literals);
		analyzer.SetSemanticsMode(true);
		analyzer.Run(parsed.root);
		analyzer.BuildSemantics(parsed.root);
		WriteSemanticsTree(out, analyzer.SemanticsTree(), 0);

		out << "end translation unit\n";
	}
	out.flush();
	if(!out)
	{
		throw runtime_error("cannot write the output file");
	}
}

void EmitTypes(const vector<string>& sources, const string& outfile)
{
	ofstream out(outfile.c_str());
	if(!out)
	{
		throw runtime_error("cannot open the output file");
	}

	string build_date;
	string build_time;
	syntax::BuildStamp(build_date, build_time);

	out << sources.size() << " translation units\n";

	for(size_t index = 0; index < sources.size(); ++index)
	{
		syntax::ParsedSource parsed;
		syntax::ParseSource(sources[index], build_date, build_time, parsed);

		out << "start translation unit " << (index + 1) << '\n';

		Model model;
		Analyzer analyzer(model, parsed.arena, parsed.spellings, parsed.literals);
		analyzer.Run(parsed.root);
		WriteScopeTree(out, model);

		out << "end translation unit\n";
	}
	out.flush();
	if(!out)
	{
		throw runtime_error("cannot write the output file");
	}
}

}  // namespace semantic
}  // namespace cppgm
