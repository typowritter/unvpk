#include <algorithm>
#include <map>
#include <set>

#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>

#include <vpk/io.h>
#include <vpk/dir.h>
#include <vpk/file.h>
#include <vpk/package.h>
#include <vpk/file_format_error.h>
#include <vpk/console_handler.h>
#include <vpk/file_data_handler_factory.h>
#include <vpk/checking_data_handler_factory.h>

namespace fs   = boost::filesystem;
namespace algo = boost::algorithm;

static std::string tolower(const std::string &s) {
	std::string s2(s);
	std::transform(s2.begin(), s2.end(), s2.begin(), (int (*)(int)) std::tolower);
	return s2;
}

static std::string &tolower(std::string &s) {
	std::transform(s.begin(), s.end(), s.begin(), (int (*)(int)) std::tolower);
	return s;
}

void Vpk::Package::read(const boost::filesystem::path &path) {
	std::string filename(path.filename());
		
	if (filename.size() < 8 || tolower(filename.substr(filename.size()-8)) != "_dir.vpk") {
		std::string msg = "file does not end in \"_dir.vpk\": \"";
		msg += path.string();
		msg += "\"";

		if (m_handler) {
			m_handler->archiveerror(Exception(msg), path.string());
			m_name = filename;
		}
		else {
			throw Exception(msg);
		}
	}
	else {
		m_name = filename.substr(0, filename.size()-8);
	}
	m_srcdir = path.parent_path().string();

	fs::ifstream is;
	is.exceptions(std::ios::failbit | std::ios::badbit);
	is.open(path, std::ios::in | std::ios::binary);
	read(is);
}

void Vpk::Package::read(const std::string &srcdir, const std::string &name, std::istream &is) {
	m_srcdir = srcdir;
	m_name   = name;
	read(is);
}

void Vpk::Package::read(std::istream &is) {
	if (readLU32(is) != 0x55AA1234) {
		is.seekg(-4, std::ios_base::cur);
	}
	else {
		unsigned int version   = readLU32(is);
		unsigned int indexSize = readLU32(is);

		if (version != 1) {
			throw FileFormatError((boost::format("unexpected vpk version %d") % version).str());
		}
	}

	// types
	while (is.good()) {
		std::string type;
		readAsciiZ(is, type);
		if (type.empty()) break;
		
		// dirs
		while (is.good()) {
			std::string path;
			readAsciiZ(is, path);
			if (path.empty()) break;

			mkpath(path).read(is, type);
		}
	}
}

Vpk::Dir &Vpk::Package::mkpath(const std::string &path) {
	std::vector<std::string> pathvec;
	algo::split(pathvec, path, algo::is_any_of("/"));
	return mkpath(pathvec);
}

Vpk::Dir &Vpk::Package::mkpath(const std::vector<std::string> &path) {
	if (path.empty()) {
		throw Exception("empty path");
	}
	
	Dir *dir = 0;
	Nodes *nodes = &m_nodes;
	for (std::vector<std::string>::const_iterator ip = path.begin(); ip != path.end(); ++ ip) {
		const std::string &name = *ip;
		Nodes::iterator in = nodes->find(name);

		if (in == nodes->end()) {
			dir = new Dir(name);
			(*nodes)[name] = NodePtr(dir);
		}
		else {
			Node *node = in->second.get();

			if (node->type() != Node::DIR) {
				std::vector<std::string> prefix;
				std::copy(path.begin(), ip+1, std::back_inserter(prefix));
				throw Exception(std::string("path is not a directory: ") + algo::join(prefix, "/"));
			}
		
			dir = (Dir*) node;
		}

		nodes = &dir->nodes();
	}

	return *dir;
}

Vpk::Node *Vpk::Package::get(const std::string &path) {
	std::vector<std::string> pathvec;
	algo::split(pathvec, path, algo::is_any_of("/"));

	if (pathvec.empty()) {
		return 0;
	}

	Node  *node  = 0;
	Nodes *nodes = &m_nodes;
	std::vector<std::string>::const_iterator ip = pathvec.begin();
	while (ip != pathvec.end()) {
		const std::string &name = *ip;
		Nodes::iterator in = nodes->find(name);

		if (in == nodes->end()) {
			return 0;
		}

		node = in->second.get();
		++ ip;

		if (ip != pathvec.end()) {
			if (node->type() != Node::DIR) {
				return 0;
			}
			nodes = &((Dir*) node)->nodes();
		}
	}

	return node;
}

static size_t filecount(const Vpk::Nodes &nodes) {
	size_t n = 0;
	for (Vpk::Nodes::const_iterator it = nodes.begin(); it != nodes.end(); ++ it) {
		Vpk::Node *node = it->second.get();
		if (node->type() == Vpk::Node::DIR) {
			n += filecount(((const Vpk::Dir*) node)->nodes());
		}
		else {
			++ n;
		}
	}

	return n;
}

size_t Vpk::Package::filecount() const {
	return ::filecount(m_nodes);
}

static void list(const Vpk::Nodes &nodes, const std::vector<std::string> &prefix, std::ostream &os) {
	for (Vpk::Nodes::const_iterator it = nodes.begin(); it != nodes.end(); ++ it) {
		Vpk::Node *node = it->second.get();
		std::vector<std::string> path(prefix);
		path.push_back(node->name());
		if (node->type() == Vpk::Node::DIR) {
			list(((const Vpk::Dir*) node)->nodes(), path, os);
		}
		else {
			os << algo::join(path, "/") << std::endl;
		}
	}
}

void Vpk::Package::list(std::ostream &os) const {
	::list(m_nodes, std::vector<std::string>(), os);
}

static void filter(Vpk::Nodes &nodes, const std::set<Vpk::Node*> &keep) {
	std::vector<std::string> erase;
	for (Vpk::Nodes::iterator it = nodes.begin(); it != nodes.end(); ++ it) {
		Vpk::Node *node = it->second.get();
		bool kept = keep.find(node) != keep.end();
		if (node->type() == Vpk::Node::DIR) {
			if (!kept) {
				Vpk::Dir *dir = (Vpk::Dir*) node;
				filter(dir->nodes(), keep);
				if (dir->nodes().empty()) {
					erase.push_back(node->name());
				}
			}
		}
		else if (!kept) {
			erase.push_back(node->name());
		}
	}

	for (std::vector<std::string>::const_iterator it = erase.begin(); it != erase.end(); ++ it) {
		nodes.erase(*it);
	}
}

std::set<std::string> Vpk::Package::filter(const std::vector<std::string> &paths) {
	std::set<std::string> notfound;
	std::set<Node*> keep;
	for (std::vector<std::string>::const_iterator i = paths.begin(); i != paths.end(); ++ i) {
		Node *node = get(*i);
		if (node) {
			keep.insert(node);
		}
		else {
			notfound.insert(*i);
		}
	}

	::filter(m_nodes, keep);

	return notfound;
}

bool Vpk::Package::error(const std::string &msg, const std::string &path, ErrorMethod handler) const {
	return error(Exception(msg + ": \"" + path + "\""), path, handler);
}

bool Vpk::Package::error(const std::exception &exc, const std::string &path, ErrorMethod handler) const {
	if (m_handler) {
		return (m_handler->*handler)(exc, path);
	}
	else {
		return true;
	}
}

void Vpk::Package::extract(const std::string &destdir, bool check) const {
	FileDataHandlerFactory factory(destdir, check);
	process(factory);
}

void Vpk::Package::check() const {
	CheckingDataHandlerFactory factory;
	process(factory);
}

void Vpk::Package::process(const Nodes &nodes,
                           const std::vector<std::string> &prefix,
                           Archives &archives,
                           DataHandlerFactory &factory) const {
	for (Nodes::const_iterator it = nodes.begin(); it != nodes.end(); ++ it) {
		const Node *node = it->second.get();
		std::vector<std::string> pathvec(prefix);
		pathvec.push_back(node->name());
		if (node->type() == Node::DIR) {
			process(((const Dir*) node)->nodes(), pathvec, archives, factory);
		}
		else {
			const File *file = (const File*) node;
			std::string path = algo::join(pathvec, "/");

			if (m_handler) m_handler->extract(path);
			boost::scoped_ptr<DataHandler> dataHandler;
				
			try {
				dataHandler.reset(factory.create(path, file->crc32));
			}
			catch (const std::exception &exc) {
				if (fileerror(exc, path)) throw;
				continue;
			}
	
			if (file->size == 0) {
				try {
					dataHandler->process((char*) &file->data[0], file->data.size());
					dataHandler->finish();
				}
				catch (const std::exception &exc) {
					if (fileerror(exc, path)) throw;
					continue;
				}
			}
			else {
				std::string archiveName = (boost::format("%s_%03d.vpk") % m_name % file->index).str();
				fs::path archivePath(m_srcdir);
				archivePath /= archiveName;
				boost::shared_ptr<fs::ifstream> archive;
						
				if (archives.find(archiveName) != archives.end()) {
					archive = archives[archiveName];
					if (!archive) {
						continue;
					}
				}
				else {
					if (!fs::exists(archivePath)) {
						archiveerror("archive does not exist", archivePath);

						archives[archiveName] = boost::shared_ptr<fs::ifstream>();
						continue;
					}
					archive.reset(new fs::ifstream);
					archive->exceptions(std::ios::failbit | std::ios::badbit);
					archive->open(archivePath, std::ios::in | std::ios::binary);
					archives[archiveName] = archive;
				}

				archive->seekg(file->offset);
				char data[BUFSIZ];
				size_t left = file->size;
				bool fail = false;
				while (left > 0) {
					size_t count = std::min(left, (size_t)BUFSIZ);
					try {
						archive->read(data, count);
					}
					catch (const std::exception& exc) {
						if (archiveerror(exc, archivePath)) throw;
						fail = true;
						break;
					}

					try {
						dataHandler->process(data, count);
					}
					catch (const std::exception& exc) {
						if (fileerror(exc, path)) throw;
						fail = true;
						break;
					}

					left -= count;
				}

				if (fail) continue;
					
				try {
					dataHandler->finish();
				}
				catch (const std::exception& exc) {
					if (fileerror(exc, path)) throw;
					continue;
				}
					
				if (!file->data.empty()) {
					std::string smallpath = path + ".smalldata";

					try {
						dataHandler.reset(factory.create(smallpath, file->crc32));
						dataHandler->process((char*) &file->data[0], file->data.size());
						dataHandler->finish();
					}
					catch (const std::exception& exc) {
						if (fileerror(exc, smallpath)) throw;
						continue;
					}
				}

				if (m_handler) m_handler->success(path);
			}
		}
	}
}

void Vpk::Package::process(DataHandlerFactory &factory) const {
	Archives archives;

	if (m_handler) m_handler->begin(*this);

	process(m_nodes, std::vector<std::string>(), archives, factory);

	if (m_handler) m_handler->end();
}