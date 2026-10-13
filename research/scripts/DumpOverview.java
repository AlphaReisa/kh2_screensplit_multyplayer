// Dumps strings (with referencing functions) and imported symbols (with callers).
//@category KH2Coop
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import ghidra.program.util.DefinedStringIterator;
import java.io.*;
import java.util.*;

public class DumpOverview extends GhidraScript {
    private String funcsOf(Address a) {
        Set<String> s = new TreeSet<>();
        for (Reference r : getReferencesTo(a)) {
            Function f = getFunctionContaining(r.getFromAddress());
            s.add(f == null ? r.getFromAddress().toString() : f.getEntryPoint().toString());
        }
        return String.join(",", s);
    }

    @Override
    public void run() throws Exception {
        String out = getScriptArgs().length > 0 ? getScriptArgs()[0] : "F:/KH2-Coop/research";
        Listing listing = currentProgram.getListing();
        try (PrintWriter w = new PrintWriter(new FileWriter(out + "/strings.tsv"))) {
            for (Data d : DefinedStringIterator.forProgram(currentProgram)) {
                Object v = d.getValue();
                String s = v == null ? "" : v.toString().replace("\t", "\\t").replace("\n", "\\n").replace("\r", "");
                w.println(d.getAddress() + "\t" + funcsOf(d.getAddress()) + "\t" + s);
            }
        }
        try (PrintWriter w = new PrintWriter(new FileWriter(out + "/imports.tsv"))) {
            SymbolTable st = currentProgram.getSymbolTable();
            for (Symbol sym : st.getExternalSymbols()) {
                Set<String> callers = new TreeSet<>();
                for (Reference r : sym.getReferences()) {
                    Address from = r.getFromAddress();
                    for (Reference r2 : getReferencesTo(from)) {
                        Function f = getFunctionContaining(r2.getFromAddress());
                        if (f != null) callers.add(f.getEntryPoint().toString());
                    }
                }
                w.println(sym.getParentNamespace().getName() + "\t" + sym.getName() + "\t" + String.join(",", callers));
            }
        }
        int n = currentProgram.getFunctionManager().getFunctionCount();
        println("functions=" + n);
    }
}
