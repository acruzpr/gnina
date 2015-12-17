/*
 * gninagrid.cpp
 *
 *  Created on: Nov 4, 2015
 *      Author: dkoes
 *
 * Output a voxelation of a provided receptor and ligand.
 * For every (heavy) atom type and grid point compute an occupancy value.
 */

#include <iostream>
#include <string>
#include <algorithm>
#include <fstream>
#include <boost/program_options.hpp>
#include <boost/multi_array.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>

#include "atom_type.h"
#include "box.h"
#include "molgetter.h"
#include "options.h"
#include "nngridder.h"

using namespace std;
using namespace boost;


//parse commandline options using boost::program_options and put the values in opts
//return true if successfull and ready to compute
//will exit on error
static bool parse_options(int argc, char *argv[], cmdoptions& o)
{
	using namespace boost::program_options;
	positional_options_description positional; // remains empty

	options_description inputs("Input");
	inputs.add_options()
	("receptor,r", value<std::string>(&o.receptorfile),
			"receptor file")
	("ligand,l", value<std::string>(&o.ligandfile), "ligand(s)");

	options_description outputs("Output");
	outputs.add_options()
	("out,o", value<std::string>(&o.outname), "output file name base, combined map if outlig not specified, receptor only otherwise")
	("outlig", value<std::string>(&o.ligoutname), "output file name base for ligand only output")
	("map", bool_switch(&o.outmap), "output AD4 map files (for debugging, out is base name)");

	options_description options("Options");
	options.add_options()
	("dimension", value<double>(&o.dim), "Cubic grid dimension (Angstroms)")
	("resolution", value<double>(&o.res), "Cubic grid resolution (Angstroms)")
	("binary_occupancy", value<bool>(&o.binary), "Output binary occupancies (still as floats)")
	("center_x", value<double>(&o.x), "X coordinate of the center, if unspecified use first ligand")
	("center_y", value<double>(&o.y), "Y coordinate of the center, if unspecified use first ligand")
	("center_z", value<double>(&o.z), "Z coordinate of the center, if unspecified use first ligand")
	("autocenter", value<string>(&o.centerfile), "ligand to use to determine center")
	("recmap", value<string>(&o.recmap), "Atom type mapping for receptor atoms")
	("ligmap", value<string>(&o.ligmap), "Atom type mapping for ligand atoms");

	options_description info("Information (optional)");
	info.add_options()
	("help", bool_switch(&o.help), "display usage summary")
	("version", bool_switch(&o.version), "display program version")
	("verbosity", value<int>(&o.verbosity)->default_value(1),
			"Adjust the verbosity of the output, default: 1");
	options_description desc;
	desc.add(inputs).add(options).add(outputs).add(info);
	variables_map vm;
	try
	{
		store(
			command_line_parser(argc, argv).options(desc)
						.style(
						command_line_style::default_style
								^ command_line_style::allow_guessing)
						.positional(positional).run(), vm);
		notify(vm);
	} catch (boost::program_options::error& e)
	{
		std::cerr << "Command line parse error: " << e.what() << '\n'
				<< "\nCorrect usage:\n" << desc << '\n';
		exit(-1);
	}

	//process informational
	if (o.help)
	{
		cout << desc << '\n';
		return false;
	}
	if (o.version)
	{
		cout << "gnina "  __DATE__ << '\n';
		return false;
	}

	return true;
}

//create a mapping from atom type ids to a unique id given a file specifying
//what types we care about (anything missing is ignored); if multiple types are
//on the same line, they are merged, if the file isn't specified, use default mapping
//return total number of types
//map is indexed by smina_atom_type, maps to -1 if type should be ignored
static int createAtomTypeMap(const string& fname, vector<int>& map)
{
	map.assign(smina_atom_type::NumTypes, -1);

	if(fname.size() == 0)
	{
		//default mapping
		int cnt = 0;
		for(int i = 0; i < smina_atom_type::NumTypes; i++)
		{
			if(!is_hydrogen((smt)i))
			{
				map[i] = cnt;
				cnt++;
			}
		}
		return cnt;
	}
	else
	{
		int cnt = 0;
		ifstream in(fname.c_str());

		if(!in)
		{
			cerr << "Could not open " << fname << "\n";
			exit(-1);
		}
		string line;
		while(getline(in, line))
		{
			vector<string> types;
			split(types, line, is_any_of("\t \n"));
			for(unsigned i = 0, n = types.size(); i < n; i++)
			{
				const string& name = types[i];
				smt t = string_to_smina_type(name);
				if(t < smina_atom_type::NumTypes) //valid
				{
					map[t] = cnt;
				}
				else if(name.size() > 0) //this ignores consecutive delimiters
				{
					cerr << "Invalid atom type " << name << "\n";
					exit(-1);
				}
			}
			if(types.size() > 0)
				cnt++;
		}
		return cnt;
	}
}



int main(int argc, char *argv[])
{
	try
	{
	//setup commandline options
	cmdoptions opt;
	if(!parse_options(argc, argv, opt))
		exit(0);

	//figure out grid center
	if(!isfinite(opt.x + opt.y + opt.z))
	{
		fl dummy; //we wil set the size
		string ligandfile = opt.ligandfile;
		if(opt.centerfile.size() > 0)
			ligandfile = opt.centerfile;
		setup_autobox(ligandfile, 0, opt.x, opt.y, opt.z, dummy, dummy, dummy);
	}

	//setup atom type mapping
	vector<int> recmap, ligmap;
	createAtomTypeMap(opt.recmap,recmap);
	createAtomTypeMap(opt.ligmap,ligmap);

	//setup receptor grid
	NNGridder gridder(opt, recmap, ligmap);
	string parmstr;

	if(!opt.outmap)
	{
		//embed grid configuration in file name
		string outname;
		ofstream binout;

		if(opt.ligoutname.size() > 1)
		{
			outname = opt.outname + "." + gridder.getParamString(true,false) + ".binmap"; //receptor  only name

			//want separate ligand/receptor grid files
			//output receptor only
			gridder.outputLMDB(outname, true, false);

			parmstr = "." + gridder.getParamString(false,true); //ligand only name
		}
		else
		{
			parmstr = "." + gridder.getParamString(true,true); //ligand and receptor name name
		}
	}

	//for each ligand..
	unsigned ligcnt = 0;
	while(gridder.readMolecule())
	{ //computes ligand grid

		//and output
		string base;
		if(opt.ligoutname.size() == 0)
			base = opt.outname + "_" + lexical_cast<string>(ligcnt);
		else
			base = opt.ligoutname + "_" + lexical_cast<string>(ligcnt);

		if(opt.outmap)
		{
			gridder.outputMAP(base);
		}
		else
		{
			string outname = base + parmstr  + ".binmap";
			gridder.outputLMDB(outname, opt.ligoutname.size() == 0);
		}
		ligcnt++;
	}

	} catch (file_error& e)
	{
		std::cerr << "\n\nError: could not open \"" << e.name.string()
				<< "\" for " << (e.in ? "reading" : "writing") << ".\n";
		return -1;
	}
}
