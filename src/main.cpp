
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <string>
#include <argagg.hpp>
#include <subprocess.h>
#include <gerbolyze.hpp>
#include "vec_core.h"
#include <base64.h>

using argagg::parser_results;
using argagg::parser;
using namespace std;
using namespace gerbolyze;

int main(int argc, char **argv) {
    parser argparser {{
            {"help", {"-h", "--help"},
                "Print help and exit",
                0},
            {"version", {"-v", "--version"},
                "Print version and exit",
                0},
            {"ofmt", {"-o", "--format"},
                "Output format. Supported: gerber, svg, s-exp (KiCAD S-Expression)",
                1},
            {"precision", {"-p", "--precision"},
                "Number of decimal places use for exported coordinates (gerber: 1-9, SVG: 0-*)",
                1},
            {"svg_clear_color", {"--clear-color"},
                "SVG color to use for \"clear\" areas (default: white)",
                1},
            {"svg_dark_color", {"--dark-color"},
                "SVG color to use for \"dark\" areas (default: black)",
                1},
            {"min_feature_size", {"-d", "--trace-space"},
                "Minimum feature size of elements in vectorized graphics (trace/space) in mm. Default: 0.1mm.",
                1},
            {"no_header", {"--no-header"},
                "Do not export output format header/footer, only export the primitives themselves",
                0},
            {"flatten", {"--flatten"},
                "Flatten output so it only consists of non-overlapping white polygons. This perform composition at the vector level. Potentially slow.",
                0},
            {"only_groups", {"-g", "--only-groups"},
                "Comma-separated list of group IDs to export.",
                1},
            {"vectorizer", {"-b", "--vectorizer"},
                "Vectorizer to use for bitmap images. One of poisson-disc (default), hex-grid, square-grid, binary-contours, dev-null.",
                1},
            {"vectorizer_map", {"--vectorizer-map"},
                "Map from image element id to vectorizer. Overrides --vectorizer. Format: id1=vectorizer,id2=vectorizer,...",
                1},
            {"force_svg", {"--force-svg"},
                "Force SVG input irrespective of file name",
                0},
            {"force_png", {"--force-png"},
                "Force bitmap graphics input irrespective of file name",
                0},
            {"size", {"-s", "--size"},
                "Bitmap mode only: Physical size of output image in mm. Format: 12.34x56.78",
                1},
            {"preserve_aspect_ratio", {"-a", "--preserve-aspect-ratio"},
                "Bitmap mode only: Preserve aspect ratio of image. Allowed values are meet, slice. Can also parse full SVG preserveAspectRatio syntax.",
                1},
            {"skip_usvg", {"--no-usvg"},
                "Do not preprocess input using usvg (do not use unless you know *exactly* what you're doing)",
                0},
            {"exclude_groups", {"-e", "--exclude-groups"},
                "Comma-separated list of group IDs to exclude from export. Takes precedence over --only-groups.",
                1},

    }};


    ostringstream usage;
    usage
        << argv[0] << " " << lib_version << endl
        << endl
        << "Usage: " << argv[0] << " [options]... [input_file] [output_file]" << endl
        << endl
        << "Specify \"-\" for stdin/stdout." << endl
        << endl;

    argagg::parser_results args;
    try {
        args = argparser.parse(argc, argv);
    } catch (const std::exception& e) {
        argagg::fmt_ostream fmt(cerr);
        fmt << usage.str() << argparser << '\n'
            << "Encountered exception while parsing arguments: " << e.what()
            << '\n';
        return EXIT_FAILURE;
    }

    if (args["help"]) {
        argagg::fmt_ostream fmt(cerr);
        fmt << usage.str() << argparser;
        return EXIT_SUCCESS;
    }

    if (args["version"]) {
        cerr << lib_version << endl;
        return EXIT_SUCCESS;
    }

    string in_f_name;
    istream *in_f = &cin;
    ifstream in_f_file;
    string out_f_name;
    ostream *out_f = &cout;
    ofstream out_f_file;

    if (args.pos.size() >= 1) {
        in_f_name = args.pos[0];

        if (args.pos.size() >= 2) {
            out_f_name = args.pos[1];
        }
    }

    if (!in_f_name.empty() && in_f_name != "-") {
        in_f_file.open(in_f_name);
        if (!in_f_file) {
            cerr << "Cannot open input file \"" << in_f_name << "\"" << endl;
            return EXIT_FAILURE;
        }
        in_f = &in_f_file;
    }

    if (!out_f_name.empty() && out_f_name != "-") {
        out_f_file.open(out_f_name);
        if (!out_f_file) {
            cerr << "Cannot open output file \"" << out_f_name << "\"" << endl;
            return EXIT_FAILURE;
        }
        out_f = &out_f_file;
    }

    bool only_polys = args["no_header"];

    int precision = 6;
    if (args["precision"]) {
        precision = atoi(args["precision"]);
    }

    string fmt = args["ofmt"] ? args["ofmt"] : "gerber";
    transform(fmt.begin(), fmt.end(), fmt.begin(), [](unsigned char c){ return std::tolower(c); }); /* c++ yeah */

    PolygonSink *sink = nullptr;
    PolygonSink *flattener = nullptr;
    if (fmt == "svg") {
        string dark_color = args["svg_dark_color"] ? args["svg_dark_color"] : "#000000";
        string clear_color = args["svg_clear_color"] ? args["svg_clear_color"] : "#ffffff";
        sink = new SimpleSVGOutput(*out_f, only_polys, precision, dark_color, clear_color);

    } else if (fmt == "gbr" || fmt == "grb" || fmt == "gerber") {
        sink = new SimpleGerberOutput(*out_f, only_polys, 4, precision);

    } else {
        cerr << "Unknown output format \"" << fmt << "\"" << endl;
        argagg::fmt_ostream fmt(cerr);
        fmt << usage.str() << argparser;
        return EXIT_FAILURE;
    }

    if (args["flatten"]) {
        flattener = new Flattener(*sink);
    }

    /* Because the C++ stdlib is bullshit */
    auto id_match = [](string in, vector<string> &out) {
        stringstream  ss(in);
        while (getline(ss, out.emplace_back(), ',')) {
        }
        out.pop_back();
    };

    IDElementSelector sel;
    if (args["only_groups"])
        id_match(args["only_groups"], sel.include);
    if (args["exclude_groups"])
        id_match(args["exclude_groups"], sel.exclude);

    string vectorizer = args["vectorizer"] ? args["vectorizer"] : "poisson-disc";
    /* Check argument */
    ImageVectorizer *vec = makeVectorizer(vectorizer);
    if (!vec) {
        cerr << "Unknown vectorizer \"" << vectorizer << "\"." << endl;
        argagg::fmt_ostream fmt(cerr);
        fmt << usage.str() << argparser;
        return EXIT_FAILURE;
    }
    delete vec;

    /*
            double min_feature_size_px = mm_to_doc_units(rset.m_minimum_feature_size_mm);
            vec->vectorize_image(cr, node, clip_path, viewport_matrix, *polygon_sink, min_feature_size_px);
            delete vec;
    */
    double min_feature_size = 0.1; /* mm */
    if (args["min_feature_size"]) {
        min_feature_size = args["min_feature_size"].as<double>();
    }

    string ending = "";
    auto idx = in_f_name.rfind(".");
    if (idx != string::npos) {
        ending = in_f_name.substr(idx);
        transform(ending.begin(), ending.end(), ending.begin(), [](unsigned char c){ return std::tolower(c); }); /* c++ yeah */
    }
    
    filesystem::path barf = { filesystem::temp_directory_path() /= (std::tmpnam(nullptr) + string(".svg")) };
    filesystem::path frob = { filesystem::temp_directory_path() /= (std::tmpnam(nullptr) + string(".svg")) };

    bool is_svg = args["force_svg"] || (ending == ".svg" && !args["force_png"]);
    if (!is_svg) {
        cerr << "writing bitmap into svg" << endl; 
        if (!args["size"]) {
            cerr << "Error: --size must be given when using bitmap input." << endl;
            argagg::fmt_ostream fmt(cerr);
            fmt << usage.str() << argparser;
            return EXIT_FAILURE;
        }

        string sz = args["size"].as<string>();
        auto pos = sz.find_first_of("x*,");
        if (pos == string::npos) {
            cerr << "Error: --size must be of form 12.34x56.78" << endl;
            argagg::fmt_ostream fmt(cerr);
            fmt << usage.str() << argparser;
            return EXIT_FAILURE;
        }

        string x_str = sz.substr(0, pos);
        string y_str = sz.substr(pos+1);

        double width = std::strtod(x_str.c_str(), nullptr);
        double height = std::strtod(y_str.c_str(), nullptr);

        if (width < 1 || height < 1) {
            cerr << "Error: --size must be of form 12.34x56.78 and values must be positive floating-point numbers in mm" << endl;
            argagg::fmt_ostream fmt(cerr);
            fmt << usage.str() << argparser;
            return EXIT_FAILURE;
        }

        ofstream svg(barf.c_str());

        svg << "<svg width=\"" << width << "mm\" height=\"" << height << "mm\" viewBox=\"0 0 "
            << width << " " << height << "\" "
            << "xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\">" << endl;

        string par_attr = "none";
        if (args["preserve_aspect_ratio"]) {
            string aspect_ratio = args["preserve_aspect_ratio"].as<string>();
            if (aspect_ratio == "meet") {
                par_attr = "xMidYMid meet";
            } else if (aspect_ratio == "slice") {
                par_attr = "xMidYMid slice";
            } else {
                par_attr = aspect_ratio;
            }
        }
        svg << "<image width=\"" << width << "\" height=\"" << height << "\" x=\"0\" y=\"0\" preserveAspectRatio=\""
            << par_attr << "\" xlink:href=\"data:image/png;base64,";
        
        /* c++ has the best hacks */
        std::ostringstream sstr;
        sstr << in_f->rdbuf();
        string le_data = sstr.str();

        svg << base64_encode(le_data);
        svg << "\"/>" << endl;

        svg << "</svg>" << endl;
        svg.close();

    } else { /* svg file */
        cerr << "copying svg input into temp svg" << endl; 

        /* c++ has the best hacks */
        std::ostringstream sstr;
        sstr << in_f->rdbuf();

        ofstream tmp_out(barf.c_str());
        tmp_out << sstr.str();
        tmp_out.close();

    }

    if (args["skip_usvg"]) {
        cerr << "skippint usvg" << endl; 
        frob = barf;

    } else {
        cerr << "calling usvg on " << barf << " and " << frob << endl; 
        const char *command_line[] = {"usvg", barf.c_str(), frob.c_str(), NULL};
        struct subprocess_s subprocess;
        int rc = subprocess_create(command_line, subprocess_option_inherit_environment, &subprocess);
        if (rc) {
            cerr << "Error calling usvg!" << endl;
            return EXIT_FAILURE;
        }

        int usvg_rc = 0;
        rc = subprocess_join(&subprocess, &usvg_rc);
        if (rc) {
            cerr << "Error calling usvg!" << endl;
            return EXIT_FAILURE;
        }
        if (usvg_rc) {
            cerr << "usvg returned an error code: " << usvg_rc << endl;
            return EXIT_FAILURE;
        }

        rc = subprocess_destroy(&subprocess);
        if (rc) {
            cerr << "Error calling usvg!" << endl;
            return EXIT_FAILURE;
        }
    }

    VectorizerSelectorizer vec_sel(vectorizer, args["vectorizer_map"] ? args["vectorizer_map"] : "");
    RenderSettings rset {
        min_feature_size,
        vec_sel,
    };

    SVGDocument doc;
    cerr << "Loading temporary file " << frob << endl;
    ifstream load_f(frob);
    if (!doc.load(load_f)) {
        cerr <<  "Error loading input file \"" << in_f_name << "\", exiting." << endl;
        return EXIT_FAILURE;
    }

    doc.render(rset, flattener ? *flattener : *sink, &sel);

    if (!is_svg) {
        remove(frob.c_str());
        remove(barf.c_str());
    }
    return EXIT_SUCCESS;
}

