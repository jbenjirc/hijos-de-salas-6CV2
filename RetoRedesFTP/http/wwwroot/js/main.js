document.getElementById("echoForm").addEventListener("submit", async e => {
  e.preventDefault();
  const msg = document.getElementById("mensaje").value;
  const resp = await fetch("/api/echo", {
    method: "POST",
    headers: { "Content-Type": "text/plain" },
    body: msg
  });
  const text = await resp.text();
  document.getElementById("respuesta").textContent = text;
});
