/*
 * toolutils.{cc,hh} -- utility routines for tools
 * Eddie Kohler
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 * Copyright (c) 2000 Mazu Networks, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>

#include <click/straccum.hh>
#include <click/bitvector.hh>
#include "routert.hh"
#include "lexert.hh"
#include "toolutils.hh"
#include <click/confparse.hh>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <csignal>
#include <dirent.h>
#include <cstdarg>

bool ignore_line_directives = false;


String
shell_command_output_string(String cmdline, const String &input, ErrorHandler *errh)
{
  FILE *f = tmpfile();
  if (!f)
    errh->fatal("cannot create temporary file: %s", strerror(errno));
  fwrite(input.data(), 1, input.length(), f);
  fflush(f);
  rewind(f);
  
  String new_cmdline = cmdline + " 0<&" + String(fileno(f));
  FILE *p = popen(new_cmdline.cc(), "r");
  if (!p)
    errh->fatal("`%s': %s", cmdline.cc(), strerror(errno));

  StringAccum sa;
  while (!feof(p) && sa.length() < 20000) {
    int x = fread(sa.reserve(2048), 1, 2048, p);
    if (x > 0) sa.forward(x);
  }
  if (!feof(p))
    errh->warning("`%s' output too long, truncated", cmdline.cc());

  fclose(f);
  pclose(p);
  return sa.take_string();
}


RouterT *
read_router_string(String text, const String &landmark, bool empty_ok,
		   ErrorHandler *errh)
{
  // check for archive
  Vector<ArchiveElement> archive;
  if (text.length() && text[0] == '!') {
    separate_ar_string(text, archive, errh);
    int found = -1;
    for (int i = 0; i < archive.size(); i++)
      if (archive[i].name == "config")
	found = i;
    if (found >= 0)
      text = archive[found].data;
    else {
      errh->lerror(landmark, "archive has no `config' section");
      text = String();
    }
  }

  // read router
  if (!text.length() && !empty_ok)
    errh->lwarning(landmark, "empty configuration");
  LexerT lexer(errh, ignore_line_directives);
  lexer.reset(text, landmark);
  RouterT *router = lexer.router();
  
  // add archive bits first
  if (router && archive.size()) {
    for (int i = 0; i < archive.size(); i++)
      if (archive[i].live() && archive[i].name != "config")
	router->add_archive(archive[i]);
  }

  // read statements
  while (lexer.ystatement())
    /* nada */;

  // done
  return lexer.finish();
}

RouterT *
read_router_string(const String &text, const String &landmark, ErrorHandler *errh)
{
  return read_router_string(text, landmark, false, errh);
}

RouterT *
read_router_file(const char *filename, bool empty_ok, ErrorHandler *errh)
{
  if (!errh)
    errh = ErrorHandler::silent_handler();

  // read file string
  int old_nerrors = errh->nerrors();
  String s = file_string(filename, errh);
  if (!s && errh->nerrors() != old_nerrors)
    return 0;

  return read_router_string(s, filename_landmark(filename), empty_ok, errh);
}

RouterT *
read_router_file(const char *filename, ErrorHandler *errh)
{
  return read_router_file(filename, false, errh);
}

RouterT *
read_router(const String &whatever, bool is_expr, ErrorHandler *errh)
{
  if (is_expr)
    return read_router_string(whatever, "<expr>", errh);
  else
    return read_router_file(String(whatever).cc(), false, errh);
}


void
write_router_file(RouterT *r, FILE *f, ErrorHandler *errh)
{
  if (!r) return;
  
  String config_str = r->configuration_string();
  
  // create archive if necessary
  const Vector<ArchiveElement> &archive = r->archive();
  if (archive.size()) {
    Vector<ArchiveElement> narchive;
    
    // add configuration
    ArchiveElement config_ae;
    config_ae.name = "config";
    config_ae.date = time(0);
    config_ae.uid = geteuid();
    config_ae.gid = getegid();
    config_ae.mode = 0644;
    config_ae.data = config_str;
    narchive.push_back(config_ae);
    
    // add other archive elements
    for (int i = 0; i < archive.size(); i++)
      if (archive[i].live() && archive[i].name != "config")
	narchive.push_back(archive[i]);

    if (narchive.size() > 1)
      config_str = create_ar_string(narchive, errh);
  }
  
  fwrite(config_str.data(), 1, config_str.length(), f);
}

int
write_router_file(RouterT *r, const char *name, ErrorHandler *errh)
{
  if (name && strcmp(name, "-") != 0) {
    FILE *f = fopen(name, "wb");
    if (!f) {
      if (errh)
	errh->error("%s: %s", name, strerror(errno));
      return -1;
    }
    write_router_file(r, f, errh);
    fclose(f);
  } else
    write_router_file(r, stdout, errh);
  return 0;
}


String
xml_quote(const String &str)
{
    const char *s = str.data();
    const char *ends = s + str.length();
    const char *first = s;
    StringAccum sa;
    for (; s < ends; s++)
	if (*s == '&' || *s == '<' || *s == '\"') {
	    sa.append(first, s - first);
	    sa << '&' << (*s == '&' ? "amp" : (*s == '<' ? "lt" : "quot")) << ';';
	    first = s + 1;
	}
    if (sa) {
	sa.append(first, s - first);
	return sa.take_string();
    } else
	return str;
}
