// Export private reference and decompilation notes for a caller-selected range.
// @category Recomp
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

public class ExportVuReferences extends GhidraScript {
  @Override
  public void run() throws Exception {
    String[] args = getScriptArgs();
    if (args.length != 3)
      throw new IllegalArgumentException("output-directory start-hex end-hex");
    File folder = new File(args[0]);
    if (!folder.isDirectory() && !folder.mkdirs())
      throw new IOException("Cannot create output directory");
    Address start = toAddr(args[1]), end = toAddr(args[2]);
    Set<Function> functions = new LinkedHashSet<>();
    try (PrintWriter out = new PrintWriter(new File(folder, "references.tsv"),
                                           StandardCharsets.UTF_8)) {
      out.println("source\tdestination\ttype\tfunction");
      var manager = currentProgram.getReferenceManager();
      var destinations = manager.getReferenceDestinationIterator(
          new AddressSet(start, end), true);
      while (destinations.hasNext() && !monitor.isCancelled()) {
        Address destination = destinations.next();
        for (Reference reference : manager.getReferencesTo(destination)) {
          Function f = getFunctionContaining(reference.getFromAddress());
          out.println(reference.getFromAddress() + "\t" + destination + "\t" +
                      reference.getReferenceType() + "\t" +
                      (f == null ? "" : f.getName()));
          if (f != null)
            functions.add(f);
        }
      }
    }
    DecompInterface decompiler = new DecompInterface();
    try {
      decompiler.openProgram(currentProgram);
      for (Function f : functions) {
        if (monitor.isCancelled())
          break;
        File file = new File(folder, "function-" + f.getEntryPoint() + ".txt");
        try (PrintWriter out = new PrintWriter(file, StandardCharsets.UTF_8)) {
          out.println("// Private analysis: " + f.getName() + " at " +
                      f.getEntryPoint());
          var result = decompiler.decompileFunction(f, 30, monitor);
          if (result.decompileCompleted())
            out.println(result.getDecompiledFunction().getC());
          else
            out.println("// Decompilation incomplete: " +
                        result.getErrorMessage());
          out.println("\n// Disassembly");
          var instructions =
              currentProgram.getListing().getInstructions(f.getBody(), true);
          while (instructions.hasNext()) {
            var i = instructions.next();
            out.println(i.getAddress() + " " + i);
          }
        }
      }
    } finally {
      decompiler.dispose();
    }
    println("Exported references and " + functions.size() +
            " functions to private directory " + folder);
  }
}
