
import java.io.*;
public class ClientUtils {
    public static void sendLine(BufferedWriter out, String s) throws IOException {
        out.write(s + "\n");
        out.flush();
    }
}