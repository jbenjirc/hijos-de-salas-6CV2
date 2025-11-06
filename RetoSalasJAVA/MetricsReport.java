import java.io.Serializable;
import java.util.concurrent.atomic.AtomicInteger;

public class MetricsReport implements Serializable {
    private static final long serialVersionUID = 1L;

    // Usamos AtomicInteger para thread-safety en el servidor
    private final AtomicInteger timeCount;
    private final AtomicInteger echoCount;
    private final AtomicInteger computeCount;

    public MetricsReport() {
        this.timeCount = new AtomicInteger(0);
        this.echoCount = new AtomicInteger(0);
        this.computeCount = new AtomicInteger(0);
    }

    public void incrementTime() {
        timeCount.incrementAndGet();
    }

    public void incrementEcho() {
        echoCount.incrementAndGet();
    }

    public void incrementCompute() {
        computeCount.incrementAndGet();
    }

    // Getters para el cliente
    public int getTimeCount() {
        return timeCount.get();
    }

    public int getEchoCount() {
        return echoCount.get();
    }

    public int getComputeCount() {
        return computeCount.get();
    }

    @Override
    public String toString() {
        // Asegúrate de usar los métodos Getters (que retornan int)
        return String.format(
                "--- Reporte de Analítica ---\n" +
                        "Invocaciones a getTime(): %d\n" +
                        "Invocaciones a echo(): %d\n" +
                        "Invocaciones a compute(): %d\n" +
                        "----------------------------",
                // Llamamos a los getters que retornan int, adecuados para %d
                getTimeCount(), getEchoCount(), getComputeCount());
    }
}