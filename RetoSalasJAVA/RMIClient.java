import java.rmi.registry.LocateRegistry;
import java.rmi.registry.Registry;
import java.util.concurrent.*;

public class RMIClient {

    private static final int NUM_THREADS = 5; // Nivel de concurrencia
    private static final int INVOCATIONS_PER_THREAD = 3; // Invocaciones por thread
    private static final int RMI_PORT = 1099;
    private static final String SERVICE_NAME = "RemoteService";
    private static final String SERVER_HOST = "localhost"; // O la IP del servidor

    public static void main(String[] args) {
        try {
            // Localizar el Registro RMI
            Registry registry = LocateRegistry.getRegistry(SERVER_HOST, RMI_PORT);

            // Obtener el stub del servicio remoto
            final RemoteService service = (RemoteService) registry.lookup(SERVICE_NAME);
            System.out.println("Conexión con el servidor RMI exitosa.");

            // Crear un pool de hilos para invocaciones concurrentes
            ExecutorService executor = Executors.newFixedThreadPool(NUM_THREADS);
            System.out.println("\nIniciando " + NUM_THREADS + " hilos para invocaciones concurrentes...");

            // Tarea (Runnable) que cada hilo ejecutará
            Runnable clientTask = () -> {
                String threadName = Thread.currentThread().getName();
                try {
                    for (int i = 1; i <= INVOCATIONS_PER_THREAD; i++) {
                        // Invocación a echo()
                        String echoMsg = "Llamada " + i + " desde " + threadName;
                        String echoResult = service.echo(echoMsg);
                        System.out.println("[" + threadName + "] Echo: " + echoResult);

                        // Invocación a compute()
                        Operation op = new Operation(Operation.Type.SUM, i * 10, i * 5);
                        Object computeResult = service.compute(op);
                        System.out.println("[" + threadName + "] Compute " + op + " = " + computeResult);

                        // Invocación a getTime()
                        service.getTime();

                        Thread.sleep(100); // Pausa corta para simular trabajo
                    }
                } catch (Exception e) {
                    System.err.println("[" + threadName + "] Error en invocación RMI: " + e.getMessage());
                }
            };

            // Enviar tareas al pool
            for (int i = 0; i < NUM_THREADS; i++) {
                executor.submit(clientTask);
            }

            // Esperar a que todos los hilos terminen
            executor.shutdown();
            executor.awaitTermination(1, TimeUnit.MINUTES);
            System.out.println("\nTodas las invocaciones concurrentes han finalizado.");

            // 5. Consultar la Analítica de Logs al finalizar
            System.out.println("\nConsultando analítica de logs...");
            MetricsReport report = service.getMetricsReport();
            System.out.println(report);

        } catch (Exception e) {
            System.err.println("Error en el cliente RMI: " + e.getMessage());
            e.printStackTrace();
        }
    }
}