/* omindex.cc: index static documents into the omega db
 *
 * ----START-LICENCE----
 * Copyright 1999,2000,2001 BrightStation PLC
 * Copyright 2001 James Aylett
 * Copyright 2001 Ananova Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 * -----END-LICENCE-----
 */

// Uncomment this if you rely on the M prefix on the mimetype or the S prefix on the
// "subsite" - these are now a T prefix on the mimetype and a H prefix for host/P prefix
// for path
//#define OLD_PREFIXES

#include <string>
#include <map>
#include <memory>

#include <fstream>

#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>

#include <om/om.h>

#include "htmlparse.h"

using std::cout;
using std::cerr;
using std::endl;

#define OMINDEX "omindex"
#define VERSION "1.0"

#define DUPE_ignore 0
#define DUPE_replace 1
#define DUPE_duplicate 2
static int dupes = DUPE_replace;
static int recurse = 1;
static string dbpath;
static string root;
static string indexroot;
static string baseurl;
static OmWritableDatabase *db;

// FIXME: these 2 copied from om/indexer/index_utils.cc
static void
lowercase_term(om_termname &term)
{
    om_termname::iterator i = term.begin();
    while (i != term.end()) {
	*i = tolower(*i);
	i++;
    }
}

class MyHtmlParser : public HtmlParser {
    public:
    	string title, sample, keywords, dump;
	bool indexing_allowed;
	void process_text(const string &text);
	void opening_tag(const string &tag, const map<string,string> &p);
	void closing_tag(const string &tag);
	MyHtmlParser() : indexing_allowed(true) { }
};

void
MyHtmlParser::process_text(const string &text)
{
    // some tags are meaningful mid-word so this is simplistic at best...
    dump += text + " ";
}

void
MyHtmlParser::opening_tag(const string &tag, const map<string,string> &p)
{
#if 0
    cout << "<" << tag;
    map<string, string>::const_iterator x;
    for (x = p.begin(); x != p.end(); x++) {
	cout << " " << x->first << "=\"" << x->second << "\"";
    }
    cout << ">\n";
#endif
    
    if (tag == "meta") {
	map<string, string>::const_iterator i, j;
	if ((i = p.find("content")) != p.end()) {
	    if ((j = p.find("name")) != p.end()) {
		string name = j->second;
		lowercase_term(name);
		if (name == "description") {
		    if (sample.empty()) {
			sample = i->second;
			decode_entities(sample);
		    }
		} else if (name == "keywords") {
		    if (!keywords.empty()) keywords += ' ';
		    string tmp = i->second;
		    decode_entities(tmp);
		    keywords += tmp;
		} else if (name == "robots") {
		    string val = i->second;
		    decode_entities(val);
		    lowercase_term(val);
		    if (val.find("none") != string::npos ||
			val.find("noindex") != string::npos) {
			indexing_allowed = false;
		    }
		}
	    }
	}
    }
}

void
MyHtmlParser::closing_tag(const string &text)
{
    string x = text;
    if (x == "title") {
	title = dump;
	// replace newlines with spaces
	size_t i = 0;    
	while ((i = title.find("\n", i)) != string::npos) title[i] = ' ';
	dump = "";
    }
}

inline static bool
p_alpha(unsigned int c)
{
    return ((c | 32) - 'a') <= ('z' - 'a');
}

inline static bool
p_notalnum(unsigned int c)
{
    return !isalnum(c);
}

inline static bool
p_notplusminus(unsigned int c)
{
    return c != '+' && c != '-';
}

static om_termpos
index_text(const string &s, OmDocument &doc, OmStem &stemmer, om_termpos pos)
{
    std::string::const_iterator i, j = s.begin(), k;
    while ((i = find_if(j, s.end(), p_alpha)) != s.end()) {
        j = find_if(i, s.end(), p_notalnum);
        k = find_if(j, s.end(), p_notplusminus);
        if (k == s.end() || !isalnum(*k)) j = k;
        om_termname term = s.substr(i - s.begin(), j - i);
        lowercase_term(term);
        if (isupper(*i) || isdigit(*i)) {
	    doc.add_posting(term, pos);
        }
 
        term = stemmer.stem_word(term);
        doc.add_posting(term, pos++);
        i = j + 1;
    }
    return pos;
}                           

static void
index_file(const string &url, const string &mimetype, time_t last_mod)
{
    string file = root + url;
    string title, sample, keywords, dump;

    cout << "Indexing \"" << baseurl << url << "\" as " << mimetype << " ... ";

    if (dupes==DUPE_ignore && db->term_exists("U" + baseurl + url)) {
	cout << "duplicate. Ignored." << endl;
	return;
    }

    if (mimetype == "text/html") {
	std::ifstream in(file.c_str());
	if (!in) {
	    cout << "can't open \"" << file << "\" - skipping\n";
	    return;
	}
	string text;
	while (!in.eof()) {
	    string line;
	    getline(in, line);
	    text += line + '\n';
	}
	in.close();
	MyHtmlParser p;
	p.parse_html(text);
	if (!p.indexing_allowed) {
	    cout << "indexing disallowed by meta tag\n";
	    return;
	}
	dump = p.dump;
	title = p.title;
	keywords = p.keywords;
	sample = p.sample;
    } else if (mimetype == "text/plain") {
	std::ifstream in(file.c_str());
	if (!in) {
	    cout << "can't open \"" << file << "\" - skipping\n";
	    return;
	}
	while (!in.eof()) {
	    string line;
	    getline(in, line);
	    dump += line + '\n';
	}
	in.close();	
    } else if (mimetype == "application/pdf") {
	string safefile = file;
	string::size_type p = 0;
	while (p < safefile.size()) {
	    if (!isalnum(safefile[p])) safefile.insert(p++, "\\");
            p++;
	}
	string tmp = "/tmp/omindex.txt";
	unlink(tmp.c_str());
	string cmd = "pdftotext " + safefile + " " + tmp;
	if (system(cmd.c_str()) != 0) {
	    cout << "pdftotext failed to extract text from \"" << file << "\" - skipping\n";
	    return;
	}
	std::ifstream in(tmp.c_str());
	unlink(tmp.c_str());
	if (!in) {
	    cout << "pdftotext failed to extract text from \"" << file << "\" - skipping\n";
	    return;
	}
	while (!in.eof()) {
	    string line;
	    getline(in, line);
	    dump += line + '\n';
	}
	in.close();
    } else if (mimetype == "application/postscript") {
	string safefile = file;
	string::size_type p = 0;
	while (p < safefile.size()) {
	    if (!isalnum(safefile[p])) safefile.insert(p++, "\\");
            p++;
	}
	string tmp = "/tmp/omindex.txt";
	unlink(tmp.c_str());
	string cmd = "pstotext -output " + tmp + ' ' + safefile;
	if (system(cmd.c_str()) != 0) {
	    cout << "pstotext failed to extract text from \"" << file << "\" - skipping\n";
	    return;
	}
	std::ifstream in(tmp.c_str());
	unlink(tmp.c_str());
	if (!in) {
	    cout << "pstotext failed to extract text from \"" << file << "\" - skipping\n";
	    return;
	}
	while (!in.eof()) {
	    string line;
	    getline(in, line);
	    dump += line + '\n';
	}
	in.close();
    } else {
	// Don't know how to index this
	cout << "unknown MIME type - skipping\n";
	return;
    }

    OmStem stemmer("english");    

    // Produce a sample
    if (sample.empty()) {
	sample = dump.substr(0, 300);
    } else {
	sample = sample.substr(0, 300);
    }
    size_t space = sample.find_last_of(" \t\n");
    if (space != string::npos) sample.erase(space);
    // replace newlines with spaces
    size_t i = 0;    
    while ((i = sample.find('\n', i)) != string::npos) sample[i] = ' ';

    // Put the data in the document
    OmDocument newdocument;
    string record = "url=" + baseurl + url + "\nsample=" + sample;
    if (title != "") record = record + "\ncaption=" + title;
    record = record + "\ntype=" + mimetype;
    newdocument.set_data(record);

    // Add postings for terms to the document
    om_termpos pos = 1;
    pos = index_text(title, newdocument, stemmer, pos);
    pos = index_text(dump, newdocument, stemmer, pos + 100);
    pos = index_text(keywords, newdocument, stemmer, pos + 100);
#ifdef OLD_PREFIXES
    newdocument.add_term_nopos("M" + mimetype); // Mimetype
    newdocument.add_term_nopos("S" + baseurl); // Subsite
#else
    newdocument.add_term_nopos("T" + mimetype); // mimeType
    string::size_type j;
    j = baseurl.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
				  "abcdefghijklmnopqrstuvwxyz"
			    	  "0123456789");
    if (j > 0 && baseurl.substr(j, 3) == "://") {
	j += 3;
    	string::size_type k = baseurl.find_first_of(":/", j);
	newdocument.add_term_nopos("H" + baseurl.substr(j, k - j)); // Host
	k = baseurl.find('/', k);
	newdocument.add_term_nopos("P" + baseurl.substr(k)); // Path
    } else {
	newdocument.add_term_nopos("P" + baseurl); // Path
    }
    struct tm *tm = localtime(&last_mod);
    char buf[9];
    sprintf(buf, "%04d%02d%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
    newdocument.add_term_nopos("D" + string(buf)); // Date (YYYYMMDD)
    buf[7] = '\0';
    if (buf[6] == '3') buf[6] = '2';
    newdocument.add_term_nopos("W" + string(buf)); // "Weak" - 10ish day interval
    buf[6] = '\0';
    newdocument.add_term_nopos("M" + string(buf)); // Month (YYYYMM)
    buf[4] = '\0';
    newdocument.add_term_nopos("Y" + string(buf)); // Year (YYYY)
#endif
    newdocument.add_term_nopos("U" + baseurl + url); // Url

    if (dupes==DUPE_replace && db->term_exists("U" + baseurl + url)) {
	// This document has already been indexed - update!
	try {
	    std::auto_ptr<OmEnquire> enq = std::auto_ptr<OmEnquire>(new OmEnquire(*db));
	    enq->set_query(OmQuery("U" + baseurl + url));
	    OmMSet mset = enq->get_mset(0, 1);
	    try {
		db->replace_document(*mset[0], newdocument);
	    } catch (...) {
	    }
	} catch (...) {
	    db->add_document(newdocument);
	    cout << "(failed re-seek) ";
	}
	cout << "duplicate. Re-indexed." << endl;
    } else {
	db->add_document(newdocument);
	cout << "done." << endl;
    }
}

static void
index_directory(const string &dir, const map<string, string>& mime_map)
{
    DIR *d;
    struct dirent *ent;
    string path = root + indexroot + dir;

    d = opendir(path.c_str());
    if (d == NULL) {
	cout << "Can't open directory \"" << path << "\" - skipping\n";
	return;
    }
    while ((ent = readdir(d)) != NULL) {
	struct stat statbuf;
	// ".", "..", and other hidden files
	if (ent->d_name[0] == '.') continue;
	string url = dir;
	if (!url.empty() && url[url.size() - 1] != '/') url += '/';
	url += ent->d_name;
	string file = root + indexroot + url;
	if (stat(file.c_str(), &statbuf) == -1) {
	    cout << "Can't stat \"" << file << "\" - skipping\n";
	    continue;
	}
	if (S_ISDIR(statbuf.st_mode)) {
	    if (!recurse) {
		continue;
	    }
	    try {
		index_directory(url, mime_map);
	    }
	    catch (...) {
		cout << "Caught unknown exception in index_directory, rethrowing" << endl;
		throw;
	    }
	    continue;
	}
	if (S_ISREG(statbuf.st_mode)) {
	    string ext;
	    string::size_type dot = url.find_last_of('.');
	    if (dot != string::npos) ext = url.substr(dot + 1);

	    map<string,string>::const_iterator mt;
	    if ((mt = mime_map.find(ext))!=mime_map.end()) {
	      // If it's in our MIME map, presumably we know how to index it
	      index_file(indexroot + url, mt->second, statbuf.st_mtime);
	    }
	    continue;
	}
	cout << "Not a regular file \"" << file << "\" - skipping\n";
    }
    closedir(d);
}

int
main(int argc, char **argv)
{
    // getopt
    char* optstring = "hvd:D:U:M:l";
    struct option longopts[8];
    int longindex, getopt_ret;

    longopts[0].name = "help";
    longopts[0].has_arg = 0;
    longopts[0].flag = NULL;
    longopts[0].val = 'h';
    longopts[1].name = "version";
    longopts[1].has_arg = 0;
    longopts[1].flag = NULL;
    longopts[1].val = 'v';
    longopts[2].name = "duplicates";
    longopts[2].has_arg = required_argument;
    longopts[2].flag = NULL;
    longopts[2].val = 'd';
    longopts[3].name = "db";
    longopts[3].has_arg = required_argument;
    longopts[3].flag = NULL;
    longopts[3].val = 'D';
    longopts[4].name = "url";
    longopts[4].has_arg = required_argument;
    longopts[4].flag = NULL;
    longopts[4].val = 'U';
    longopts[5].name = "mime-type";
    longopts[5].has_arg = required_argument;
    longopts[5].flag = NULL;
    longopts[5].val = 'M';
    longopts[6].name = "no-recurse";
    longopts[6].has_arg = 0;
    longopts[6].flag = NULL;
    longopts[6].val = 'l';
    longopts[7].name = 0;
    longopts[7].has_arg = 0;
    longopts[7].flag = 0;
    longopts[7].val = 0;

    map<string, string> mime_map = map<string, string>();
    mime_map["txt"] = "text/plain";
    mime_map["text"] = "text/plain";
    mime_map["html"] = "text/html";
    mime_map["htm"] = "text/html";
    mime_map["shtml"] = "text/html";
    mime_map["pdf"] = "application/pdf";
    mime_map["ps"] = "application/postscript";
    mime_map["eps"] = "application/postscript";
    mime_map["ai"] = "application/postscript";

    while ((getopt_ret = getopt_long(argc, argv, optstring,
				     longopts, &longindex))!=EOF) {
	switch (getopt_ret) {
	case 'h':
	    cout << OMINDEX << endl
		 << "Usage: " << argv[0] << " [OPTION] --db DATABASE "
		 << endl << "\t--url BASEURL [BASEDIRECTORY] DIRECTORY" << endl << endl
		 << "Index static website data via the filesystem." << endl
		 << "  -d, --duplicates\tset duplicate handling" << endl
		 << "  \t\t\tone of `ignore', `replace', `duplicate'" << endl
		 << "  -D, --db\t\tpath to database to use" << endl
		 << "  -U, --url\t\tbase url DIRECTORY represents" << endl
	         << "  -M, --mime-type\tadditional MIME mapping ext:type" << endl
		 << "  -l, --no-recurse\tonly process the given directory" << endl
		 << "  -h, --help\t\tdisplay this help and exit" << endl
		 << "  -v --version\t\toutput version and exit" << endl << endl
		 << "Report bugs via the web interface at:" << endl
		 << "<http://sourceforge.net/tracker/?func=add&group_id=35626&atid=414875>" << endl;
	    return 0;
	    break;
	case 'v':
	    cout << OMINDEX << " (omega) " << VERSION << endl;
	    cout << "(c) Copyright 1999,2000,2001 BrightStation PLC." << endl;
	    cout << "(c) Copyright 2001 James Aylett" << endl << endl;
	    cout << "This is free software, and may be redistributed under" << endl;
	    cout << "the terms of the GNU Public License." << endl;
	    return 0;
	    break;
	case 'd': // how shall we handle duplicate documents?
	    switch (optarg[0]) {
	    case 'd':
		dupes = DUPE_duplicate;
		break;
	    case 'i':
		dupes = DUPE_ignore;
		break;
	    case 'r':
		dupes = DUPE_replace;
		break;
	    }
	    break;
	case 'l': // Turn off recursion
	    recurse = 0;
	    break;
	case 'M':
	    {
		char* s;
		if ((s = strchr(optarg, ':'))!=NULL && s[1]!=0) {
		    mime_map[string(optarg, s - optarg)] = string(s+1);
		} else {
		    cerr << "Illegal MIME mapping '" << optarg << "'" << endl;
		    cerr << "Should be of the form ext:type, eg txt:text/plain" << endl;
		    return 1;
		}
	    }
	    break;
	case 'D':
	    dbpath = optarg;
	    break;
	case 'U':
	    baseurl = optarg;
	    break;
	case ':': // missing param
	    return 1;
	case '?': // unknown option: FIXME -> char
	    return 1;
	}
    }

    if (dbpath=="") {
	cerr << OMINDEX << ": you must specify a database with --db.\n";
	return 1;
    }
    if (baseurl=="") {
	cerr << OMINDEX << ": you must specify a base URL with --url.\n";
	return 1;
    }
    // baseurl mustn't end '/' or you end up with the wrong URL
    // (//thing is different to /thing). We could probably make this
    // safe a different way, by ensuring that we don't put a leading '/'
    // on leafnames when scanning a directory, but this will do.
    if (baseurl[baseurl.length()-1]=='/') {
	cout << "baseurl has trailing '/' ... removing ... " << endl;
	baseurl = baseurl.substr(0, baseurl.length()-1);
    }

    if (optind >= argc || optind+2 < argc) {
	cerr << OMINDEX << ": you must specify a directory to index.\nDo this either as a single directory (taken to be the store of the base URL)\nor two, one the store of the base URL and one a dir within that to index.";
	return 1;
    }
    root = argv[optind];
    if (optind+2 == argc) {
	indexroot = argv[optind+1]; // relative to root
	if (indexroot[0]!='/') {
	    indexroot = "/" + indexroot;
	}
    } else {
	indexroot = ""; // index the whole of root
    }

    OmSettings params;
    params.set("backend", "quartz");
    params.set("quartz_dir", dbpath);
    try {
	try {
	    db = new OmWritableDatabase(params);
	} catch (OmOpeningError& error) {
	    params.set("database_create", true);
	    db = new OmWritableDatabase(params);
	}
	index_directory("/", mime_map);
	//      db->reopen(); // Ensure we're up to date
	//      cout << "\n\nNow we have " << db->get_doccount() << " documents.\n";
	delete db;
    }
    catch (OmError &e) {
	cout << "Exception: " << e.get_msg() << endl;
    }
    catch (string &s) {
	cout << "Exception: " << s << endl;
    }
    catch (const char *s) {
	cout << "Exception: " << s << endl;
    }
    catch (...) {
	cout << "Caught unknown exception" << endl;
    }
    return 0;   
}
