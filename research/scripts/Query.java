// Batch query tool. Args: <cmdfile> <outfile>
// cmd lines:  dec <addr>     -> decompile function containing addr
//             xref <addr>    -> list references to addr (from addr + containing function)
//             callers <addr> -> unique functions calling/referencing addr
//@category KH2Coop
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import java.io.*;
import java.nio.file.*;
import java.util.*;

public class Query extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        List<String> cmds = Files.readAllLines(Paths.get(a[0]));
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        try (PrintWriter w = new PrintWriter(new FileWriter(a[1]))) {
            for (String line : cmds) {
                line = line.trim();
                if (line.isEmpty() || line.startsWith("#")) continue;
                String[] p = line.split("\\s+");
                Address ad = toAddr(p[1]);
                w.println("==== " + line);
                if (p[0].equals("dec")) {
                    Function f = getFunctionContaining(ad);
                    if (f == null) { w.println("no function"); continue; }
                    DecompileResults r = di.decompileFunction(f, 120, monitor);
                    w.println(r.getDecompiledFunction() == null ? r.getErrorMessage() : r.getDecompiledFunction().getC());
                } else if (p[0].equals("xref")) {
                    for (Reference r : getReferencesTo(ad)) {
                        Function f = getFunctionContaining(r.getFromAddress());
                        w.println(r.getFromAddress() + "\t" + r.getReferenceType() + "\t" + (f == null ? "-" : f.getEntryPoint()));
                    }
                } else if (p[0].equals("dis")) {
                    int n = p.length > 2 ? Integer.parseInt(p[2]) : 30;
                    Instruction ins = getInstructionAt(ad);
                    if (ins == null) { disassemble(ad); ins = getInstructionAt(ad); }
                    for (int i = 0; i < n && ins != null; i++, ins = ins.getNext())
                        w.println(ins.getAddress() + "  " + ins);
                } else if (p[0].equals("callers")) {
                    Set<String> s = new TreeSet<>();
                    for (Reference r : getReferencesTo(ad)) {
                        Function f = getFunctionContaining(r.getFromAddress());
                        s.add(f == null ? r.getFromAddress().toString() : f.getEntryPoint().toString());
                    }
                    w.println(String.join("\n", s));
                }
                w.flush();
            }
        }
        di.dispose();
    }
}
