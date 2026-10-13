// Args: <start> <end> <outfile>  -> every reference into [start,end) with containing function
//@category KH2Coop
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import java.io.*;

public class RangeXref extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        Address s = toAddr(a[0]), e = toAddr(a[1]);
        ReferenceManager rm = currentProgram.getReferenceManager();
        try (PrintWriter w = new PrintWriter(new FileWriter(a[2]))) {
            for (Address x = s; x.compareTo(e) < 0; x = x.add(1)) {
                for (Reference r : rm.getReferencesTo(x)) {
                    Function f = getFunctionContaining(r.getFromAddress());
                    w.println(x + "\t" + r.getFromAddress() + "\t" + r.getReferenceType() + "\t" + (f == null ? "-" : f.getEntryPoint().toString()));
                }
            }
        }
    }
}
