# Makefile for preempt/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = preempt_none.la

preempt_none_la_SOURCES = preempt_none.c

preempt_none_la_LDFLAGS = $(PLUGIN_FLAGS)

