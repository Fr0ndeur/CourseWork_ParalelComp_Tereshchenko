async function apiGet(path) {
    const r = await fetch(path, { method: "GET" });
    const t = await r.text();
    try { return JSON.parse(t); } catch { return { ok: false, raw: t }; }
  }
  
  async function apiPost(path, bodyObj) {
    const r = await fetch(path, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(bodyObj),
    });
    const t = await r.text();
    try { return JSON.parse(t); } catch { return { ok: false, raw: t }; }
  }
  
  function pretty(x) {
    return typeof x === "string" ? x : JSON.stringify(x, null, 2);
  }
  
  async function refreshStatus() {
    const st = await apiGet("/status");
    document.getElementById("statusOut").textContent = pretty(st);
  
    // auto-fill UI from status
    if (st && st.ok) {
      const dp = document.getElementById("datasetPath");
      const th = document.getElementById("threads");
      if (!dp.value) dp.value = st.dataset_path || "";
      if (th.value) {} else th.value = st.build_threads || 4;
  
      document.getElementById("schedEnabled").checked = !!st.scheduler_enabled;
      document.getElementById("schedInterval").value = st.scheduler_interval_s || 30;
    }
  }
  
  document.getElementById("btnStatus").addEventListener("click", refreshStatus);
  
  document.getElementById("btnBuild").addEventListener("click", async () => {
    const dataset_path = document.getElementById("datasetPath").value.trim();
    const threads = parseInt(document.getElementById("threads").value || "4", 10);
    const incremental = document.getElementById("incremental").checked;
  
    const res = await apiPost("/build", { dataset_path, threads, incremental });
    document.getElementById("buildOut").textContent = pretty(res);
    await refreshStatus();
  });
  
  document.getElementById("btnSched").addEventListener("click", async () => {
    const enabled = document.getElementById("schedEnabled").checked;
    const interval_s = parseInt(document.getElementById("schedInterval").value || "30", 10);
    const res = await apiPost("/scheduler", { enabled, interval_s });
    document.getElementById("schedOut").textContent = pretty(res);
    await refreshStatus();
  });
  
  document.getElementById("btnSearch").addEventListener("click", async () => {
    const q = document.getElementById("query").value.trim();
    const topk = parseInt(document.getElementById("topk").value || "20", 10);
  
    const res = await apiGet(`/search?q=${encodeURIComponent(q)}&topk=${topk}`);
    document.getElementById("searchOut").textContent = pretty(res);
  });
  
  refreshStatus();
  