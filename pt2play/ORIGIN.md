# ProTracker replay source origin

This directory contains the standalone playback subset adapted from the local
ProTracker 2.3D Clone source by Olav “8bitbubsy” Sørensen.

The replay/effect routine comes from `src/pt2_replayer.c`. Paula mixing, BLEP
synthesis, Amiga filters, oversampling/downsampling, replay tables, and loader
behavior come from the corresponding `src/pt2_paula.*`, `src/pt2_blep.*`,
`src/pt2_rcfilters.*`, `src/pt2_downsample2x.*`, `src/pt2_tables.c`,
`src/modloaders/pt2_load_mod31.c`, `src/modloaders/pt2_load_mod15.c`, and
`src/pt2_module_loader.c` files.

SDL/editor integration was replaced by the small compatibility and public API
layers in `pt2_internal.h`, `pt2play.h`, and `pt2play.c`. The original license
is reproduced in `LICENSE.txt`.
