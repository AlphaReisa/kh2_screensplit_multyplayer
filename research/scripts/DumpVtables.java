// Dumps every RTTI vftable: class, address, slot -> function.
//@category KH2Coop
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import java.io.*;

public class DumpVtables extends GhidraScript {
    @Override
    public void run() throws Exception {
        String out = getScriptArgs().length > 0 ? getScriptArgs()[0] : "F:/KH2-Coop/research";
        Memory mem = currentProgram.getMemory();
        AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        try (PrintWriter w = new PrintWriter(new FileWriter(out + "/vtables.tsv"))) {
            SymbolIterator it = currentProgram.getSymbolTable().getAllSymbols(true);
            while (it.hasNext()) {
                Symbol s = it.next();
                if (!s.getName().contains("vftable")) continue;
                String cls = s.getParentNamespace().getName(true);
                Address a = s.getAddress();
                w.println("VT\t" + cls + "\t" + a + "\t" + s.getName());
                for (int i = 0; i < 128; i++) {
                    Address slot = a.add(i * 8L);
                    if (i > 0 && currentProgram.getSymbolTable().getPrimarySymbol(slot) != null
                        && currentProgram.getSymbolTable().getPrimarySymbol(slot).getName().contains("meta")) break;
                    long p;
                    try { p = mem.getLong(slot); } catch (Exception e) { break; }
                    Address t = sp.getAddress(p);
                    Function f = getFunctionAt(t);
                    if (f == null) {
                        if (!mem.contains(t) || !mem.getBlock(t).isExecute()) break;
                    }
                    w.println("\t" + i + "\t" + t + "\t" + (f == null ? "?" : f.getName()));
                    if (i > 0 && currentProgram.getSymbolTable().getPrimarySymbol(slot.add(8)) != null
                        && currentProgram.getSymbolTable().getPrimarySymbol(slot.add(8)).getName().contains("vftable")) break;
                }
            }
        }
    }
}
