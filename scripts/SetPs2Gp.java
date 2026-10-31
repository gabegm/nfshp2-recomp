// @category Recomp
import ghidra.app.script.GhidraScript;
import java.math.BigInteger;
public class SetPs2Gp extends GhidraScript {
  public void run() throws Exception {
    String[] args = getScriptArgs();
    if (args.length != 1)
      throw new IllegalArgumentException("ELF global pointer in hex");
    var gp = currentProgram.getRegister("gp");
    if (gp == null)
      throw new IllegalArgumentException(
          "No gp register in selected processor");
    for (var block : currentProgram.getMemory().getBlocks())
      if (block.isExecute() && block.getName().equals(".text"))
        currentProgram.getProgramContext().setValue(
            gp, block.getStart(), block.getEnd(), new BigInteger(args[0], 16));
    println("Applied ELF global pointer to .text");
  }
}
