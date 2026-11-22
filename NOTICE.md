# Third-party components

Receiver framing, ephemeris handling and the solver integration are based on the Trimble 5700 terminal collector, copyright (c) 2026 Trimble 5700 terminal collector contributors (MIT). Its license is in `licenses/testing.txt`.

RTKLIB v2.4.3-b34, copyright (c) 2007–2020 T. Takasu, is distributed under its BSD license and the component notices in `licenses/RTKLIB.txt`.

Surveyed-ground triangulation uses [delaunator-cpp v0.4.0](https://github.com/delfrrr/delaunator-cpp/tree/393cfc39c9b1ca268081d5f42062de0d9b33f5a2), © 2018 Volodymyr Bilonenko, under the [MIT license](licenses/delaunator.txt).

The CGG2013a NAD83(CSRS) grid is derived from work by Natural Resources Canada, distributed under the [Open Government Licence – Canada](https://open.canada.ca/en/open-government-licence-canada). Grid: https://cdn.proj.org/ca_nrc_CGG2013an83.tif. PROJ performs the grid lookup.

Qt 6 and PROJ are linked dependencies. Android USB communication uses [usb-serial-for-android](https://github.com/mik3y/usb-serial-for-android) v3.9.0 under the MIT license. Its component notices accompany the Android sources.

The practice-pad orthophoto and Ontario aerial basemap contain information licensed under the [Open Government Licence – Ontario](https://www.ontario.ca/page/open-government-licence-ontario). Source: [Geospatial Ontario Imagery Web Map Service](https://geohub.lio.gov.on.ca/maps/101295c5d3424045917bdd476f322c02/about); the acquisition index identifies DRAPE 2024 at the practice pad. `tools/practice_aerial.py` packages its local raster tiles in MBTiles format. Street map data © OpenStreetMap contributors; the global satellite overview is NASA GIBS Landsat WELD.
