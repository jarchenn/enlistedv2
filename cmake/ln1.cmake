# LN1 (docs/lines.md): line features - one Instance per drawn line, the line-type registry and builders, lasso Line mode
target_sources(NormandyMapmaker PRIVATE src/lines.cpp src/lasso_line.cpp)
target_sources(NormandyMapmaker PRIVATE src/lines_ln2.cpp)   # LN2: fence / wire / wall / hedge line types, gates, migration
target_sources(NormandyMapmaker PRIVATE src/selftest_lines_ln2.cpp)   # LN2: --selftest-lines (stages 4600-4699)
