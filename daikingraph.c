// Daikin graph from mariadb
// Copyright (c) 2022 Adrian Kennard, Andrews & Arnold Limited, see LICENSE file (GPL)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <popt.h>
#include <err.h>
#include <curl/curl.h>
#include <sqllib.h>
#include <axl.h>
#include <math.h>

int debug = 0;

int main(int argc, const char *argv[])
{
   const char *sqlhostname = NULL;
   const char *sqldatabase = "env";
   const char *sqlusername = NULL;
   const char *sqlpassword = NULL;
   const char *sqlconffile = NULL;
   const char *sqltable = "daikin";
   const char *tag = NULL;
   const char *title = NULL;
   const char *control = NULL;
   const char *me = NULL;
   const char *targetcol = "#080";
   const char *envcol = "#800";
   const char *homecol = "#880";
   const char *liquidcol = "#008";
   const char *inletcol = "#808";
   const char *outsidecol = "#088";
   const char *date = NULL;
   double xsize = 36;           // Per hour
   double ysize = 36;           // Per degree
   double left = 36;            // Left margin
   int debug = 0;
   int nogrid = 0;
   int noaxis = 0;
   int nolabels = 0;
   int back = 0;
   int temptop = 0;
   {                            // POPT
      poptContext optCon;       // context for parsing command-line options
      const struct poptOption optionsTable[] = {
         { "sql-conffile", 'c', POPT_ARG_STRING, &sqlconffile, 0, "SQL conf file", "filename" },
         { "sql-hostname", 'H', POPT_ARG_STRING, &sqlhostname, 0, "SQL hostname", "hostname" },
         { "sql-database", 'd', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &sqldatabase, 0, "SQL database", "db" },
         { "sql-username", 'U', POPT_ARG_STRING, &sqlusername, 0, "SQL username", "name" },
         { "sql-password", 'P', POPT_ARG_STRING, &sqlpassword, 0, "SQL password", "pass" },
         { "sql-table", 't', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &sqltable, 0, "SQL table", "table" },
         { "sql-debug", 'v', POPT_ARG_NONE, &sqldebug, 0, "SQL Debug" },
         { "x-size", 0, POPT_ARG_DOUBLE | POPT_ARGFLAG_SHOW_DEFAULT, &xsize, 0, "X size per hour", "pixels" },
         { "y-size", 0, POPT_ARG_DOUBLE | POPT_ARGFLAG_SHOW_DEFAULT, &ysize, 0, "Y size per step", "pixels" },
         { "tag", 'i', POPT_ARG_STRING, &tag, 0, "Device ID", "tag" },
         { "date", 'D', POPT_ARG_STRING, &date, 0, "Date", "YYYY-MM-DD" },
         { "title", 'T', POPT_ARG_STRING, &title, 0, "Title", "text" },
         { "temp-top", 0, POPT_ARG_INT, &temptop, 0, "Top temp", "C" },
         { "back", 0, POPT_ARG_INT, &back, 0, "Back days", "N" },
         { "control", 'C', POPT_ARG_STRING, &control, 0, "Control", "[-]N[T/C/R]" },
         { "no-grid", 0, POPT_ARG_NONE, &nogrid, 0, "No grid lines" },
         { "no-axis", 0, POPT_ARG_NONE, &noaxis, 0, "No axis labels" },
         { "no-labels", 0, POPT_ARG_NONE, &nolabels, 0, "No labels" },
         { "debug", 'V', POPT_ARG_NONE, &debug, 0, "Debug" },
         { "target-colour", 0, POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &targetcol, 0, "Target colour", "#rgb" },
         { "env-colour", 0, POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &envcol, 0, "Env colour", "#rgb" },
         { "home-colour", 0, POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &homecol, 0, "Home colour", "#rgb" },
         { "liquid-colour", 0, POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &liquidcol, 0, "Liquid colour", "#rgb" },
         { "inlet-colour", 0, POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &inletcol, 0, "Inlet colour", "#rgb" },
         { "outside-colour", 0, POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &outsidecol, 0, "Outside colour", "#rgb" },
         { "me", 0, POPT_ARG_STRING | POPT_ARGFLAG_DOC_HIDDEN, &me, 0, "Me link", "URL" },
         POPT_AUTOHELP { }
      };

      optCon = poptGetContext(NULL, argc, argv, optionsTable, 0);
      //poptSetOtherOptionHelp (optCon, "");

      int c;
      if ((c = poptGetNextOpt(optCon)) < -1)
         errx(1, "%s: %s\n", poptBadOption(optCon, POPT_BADOPTION_NOALIAS), poptStrerror(c));

      if (poptPeekArg(optCon))
      {
         poptPrintUsage(optCon, stderr, 0);
         return -1;
      }
      poptFreeContext(optCon);
   }
   if (noaxis)
      left = 0;

   if (control)
   {                            // PATH_INFO typically
      if (*control == '/')
         control++;
      date = control;
      char *s = strchr(control, '/');
      if (s)
      {
         *s++ = 0;
         tag = s;
      }
   }

   if (!tag || !*tag)
      errx(1, "Specify --tag");
   if (!date || !*date)
      errx(1, "Specify --date");

   time_t sod,
    eod;                        // Start and end of day
   int hours = 0;               // Number of hours
   double mintemp = NAN,
       maxtemp = NAN;           // Min and max temps seen
   {
      int Y,
       M,
       D;
      if (sscanf(date, "%d-%d-%d", &Y, &M, &D) != 3)
         errx(1, "Bad date");
      struct tm t = {.tm_year = Y - 1900,.tm_mon = M - 1,.tm_mday = D,.tm_isdst = -1 };
      sod = mktime(&t);
      t.tm_mday++;
      t.tm_isdst = -1;
      eod = mktime(&t);
      hours = (eod - sod) / 3600;
   }

   SQL sql;
   sql_real_connect(&sql, sqlhostname, sqlusername, sqlpassword, sqldatabase, 0, NULL, 0, 1, sqlconffile);

   xml_t svg = xml_tree_new("svg");
   if (me)
      xml_addf(svg, "a@rel=me@href", me);
   xml_element_set_namespace(svg, xml_namespace(svg, NULL, "http://www.w3.org/2000/svg"));
   xml_t top = xml_element_add(svg, "g");       // Top level, adjusted for position as temps all plotted from 0C as Y=0
   xml_t axis = xml_element_add(top, "g");      // Axis labels
   xml_t grid = xml_element_add(top, "g");      // Grid 1C/1hour
   xml_t ranges = xml_element_add(top, "g");    // Ranges
   xml_t traces = xml_element_add(top, "g");    // Traces
   xml_t labels = xml_element_add(svg, "g");    // Title (not offset)

   double utcx(SQL_RES * res) {
      char *utc = sql_colz(res, "utc");
      if (!utc)
         return NAN;
      time_t t = sql_time_utc(utc);
      return xsize * (t - sod) / 3600;
   }

   double tempy(SQL_RES * res, const char *field) {
      char *val = sql_col(res, field);
      if (!val || !*val)
         return NAN;
      double temp = strtod(val, NULL);
      if (isnan(mintemp) || mintemp > temp)
         mintemp = temp;
      if (isnan(maxtemp) || maxtemp < temp)
         maxtemp = temp;
      return temp * ysize;
   }

   void addpos(FILE * f, char *m, double x, double y) {
      if (isnan(x) || isnan(y))
         return;
      fprintf(f, "%c%.2f,%.2f", *m, x, y);
      *m = 'L';
   }

   const char *range(xml_t g, const char *field, const char *colour, int group) {       // Plot a temp range based on min/max of field
      char *min,
      *max;
      if (asprintf(&min, "min%s", field) < 0 || asprintf(&max, "max%s", field) < 0)
         errx(1, "malloc");
      char *path;
      size_t len;
      FILE *f = open_memstream(&path, &len);
      char m = 'M';
      double last;
      SQL_RES *select(const char *order) {
         return sql_safe_query_store_free(&sql, sql_printf("SELECT min(`utc`) AS `utc`,max(`%#S`) AS `%#S`,min(`%#S`) AS `%#S` FROM `%#S` WHERE `tag`=%#s AND `utc`>=%#U AND `utc`<=%#U GROUP BY substring(`utc`,1,%d) ORDER BY `utc` %s", max, max, min, min, sqltable, tag, sod, eod, group, order));
      }
      // Forward
      last = NAN;
      SQL_RES *res = select("asc");
      while (sql_fetch_row(res))
      {
         double t = tempy(res, max);
         addpos(f, &m, utcx(res), isnan(last) || t > last ? t : last);
         last = t;
      }
      sql_free_result(res);
      // Reverse
      res = select("desc");
      last = NAN;
      double lastx = NAN;
      while (sql_fetch_row(res))
      {
         double t = tempy(res, min);
         if (!isnan(lastx))
            addpos(f, &m, lastx, isnan(last) || t < last ? t : last);
         last = t;
         lastx = utcx(res);
      }
      if (!isnan(lastx))
         addpos(f, &m, lastx, last);
      sql_free_result(res);
      fclose(f);
      if (*path)
      {
         xml_t p = xml_element_add(g, "path");
         xml_add(p, "@d", path);
         xml_add(p, "@fill", colour);
         xml_add(p, "@stroke", colour);
         xml_add(p, "@opacity", "0.1");
      } else
         colour = NULL;
      free(path);
      free(min);
      free(max);
      return colour;
   }
   const char *trace(xml_t g, const char *field, const char *colour) {  // Plot trace
      char *path;
      size_t len;
      FILE *f = open_memstream(&path, &len);
      char m = 'M';
      // Forward
      SQL_RES *res = sql_safe_query_store_free(&sql, sql_printf("SELECT `utc`,`%#S` FROM `%#S` WHERE `tag`=%#s AND `utc`>=%#U AND `utc`<=%#U ORDER BY `utc`", field, sqltable, tag, sod, eod));
      while (sql_fetch_row(res))
         addpos(f, &m, utcx(res), tempy(res, field));
      sql_free_result(res);
      fclose(f);
      if (*path)
      {
         xml_t p = xml_element_add(g, "path");
         xml_add(p, "@d", path);
         xml_add(p, "@fill", "none");
         xml_add(p, "@stroke", colour);
      } else
         colour = NULL;
      free(path);
      return colour;
   }
   const char *rangetrace(xml_t g, xml_t g2, const char *field, const char *colour) {   // Plot a temp range based on min/max of field and trace
      const char *col = range(g, field, colour, 15);
      trace(g2, field, colour);
      return col;
   }

   targetcol = range(ranges, "target", targetcol, 19);
   envcol = rangetrace(ranges, traces, "env", envcol);
   homecol = rangetrace(ranges, traces, "home", homecol);
   liquidcol = rangetrace(ranges, traces, "liquid", liquidcol);
   inletcol = rangetrace(ranges, traces, "inlet", inletcol);
   outsidecol = rangetrace(ranges, traces, "outside", outsidecol);

   // Set range of temps shown
   mintemp = floor(mintemp) - 0.5;
   maxtemp = ceil(maxtemp) + 0.5;
   // Grid
   if (!nogrid)
   {
      char *path;
      size_t len;
      FILE *f = open_memstream(&path, &len);
      char m;
      for (int h = 0; h <= hours; h++)
      {
         m = 'M';
         addpos(f, &m, xsize * h, ysize * mintemp);
         addpos(f, &m, xsize * h, ysize * maxtemp);
      }
      for (double t = ceil(mintemp); t <= floor(maxtemp); t += 1)
      {
         m = 'M';
         addpos(f, &m, 0, ysize * t);
         addpos(f, &m, xsize * hours, ysize * t);
      }
      m = 'M';                  // Extra on zero
      addpos(f, &m, 0, 0);
      addpos(f, &m, xsize * hours, 0);
      fclose(f);
      if (*path)
      {
         xml_t p = xml_element_add(grid, "path");
         xml_add(p, "@d", path);
         xml_add(p, "@fill", "none");
         xml_add(p, "@stroke", "black");
         xml_add(p, "@opacity", "0.25");
      }
      free(path);
   }
   // Axis
   if (!noaxis)
   {
      for (int h = 0; h < hours; h++)
      {
         struct tm tm;
         time_t when = sod + 3600 * h;
         localtime_r(&when, &tm);
         xml_t t = xml_addf(axis, "+text", "%02d", tm.tm_hour);
         xml_addf(t, "@x", "%.2f", xsize * h);
         xml_add(t, "@y", "-1");
      }
      for (double temp = ceil(mintemp); temp <= floor(maxtemp); temp += 1)
      {
         xml_t t = xml_addf(axis, "+text", "%.0f", temp);
         xml_add(t, "@x", "-1");
         xml_addf(t, "@y", "%.2f", ysize * temp + 7);
         xml_add(t, "@text-anchor", "end");
      }
   }
   // Title
   {
      int y = 0;
      if (title)
      {
         char *txt = strdupa(title);
         while (txt && *txt)
         {
            char *e = strchr(txt, '/');
            if (e)
               *e++ = 0;
            xml_t t = xml_element_add(labels, "text");
            if (*txt == '-')
            {
               y += 9;
               txt++;
               xml_add(t, "@font-size", "7");
            } else
               y += 17;
            xml_element_set_content(t, txt);
            xml_addf(t, "@x", "%.2f", xsize * hours + left - 1);
            xml_addf(t, "@y", "%d", y);
            xml_add(t, "@text-anchor", "end");
            txt = e;
         }
      }
      if (!nolabels)
      {
         void label(const char *text, const char *colour) {
            if (!colour)
               return;
            y += 17;
            xml_t t = xml_element_add(labels, "text");
            xml_element_set_content(t, text);
            xml_addf(t, "@x", "%.2f", xsize * hours + left - 1);
            xml_addf(t, "@y", "%d", y);
            xml_add(t, "@text-anchor", "end");
            xml_add(t, "@fill", colour);
            warnx(text);
         }
	 label(date,"black");
	 label(tag,"black");
         label("Target", targetcol);
         label("Env", envcol);
         label("Home", homecol);
         label("Liquid", liquidcol);
         label("Inlet", inletcol);
         label("Outside", outsidecol);
      }
   }
   // Set width/height/offset
   xml_addf(svg, "@width", "%.0f", xsize * hours + left);
   xml_addf(svg, "@height", "%.0f", ysize * (maxtemp - mintemp));
   xml_addf(top, "@transform", "translate(%.1f,%.1f)scale(1,-1)", left, ysize * maxtemp);
   xml_add(svg, "@font-family", "sans-serif");
   xml_add(svg, "@font-size", "15");
   // Write out
   xml_write(stdout, svg);
   xml_tree_delete(svg);
   sql_close(&sql);
   return 0;
}