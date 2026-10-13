#!/bin/bash
# usage: q.sh cmds.txt out.txt
. /f/KH2-Coop/env.sh
"$GHIDRA_INSTALL_DIR/support/analyzeHeadless.bat" "F:/KH2-Coop/ghidra-projects" KH2FM -process KH2FM.exe -noanalysis -scriptPath "F:/KH2-Coop/research/scripts" -postScript Query.java "$(cygpath -m "$1")" "$(cygpath -m "$2")" > /f/KH2-Coop/research/q.log 2>&1
grep -E "ERROR|Exception" /f/KH2-Coop/research/q.log | head -5
