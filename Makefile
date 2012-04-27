
AM_FEATURES = libusual

noinst_PROGRAMS = dbproxy

dbproxy_SOURCES = main.c
dbproxy_MERGE_LIBUSUAL = 1

USUAL_DIR = $(HOME)/src/libusual

include $(shell antimake.mk)

