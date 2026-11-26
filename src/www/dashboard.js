// History lives on the controller. The browser only caches the displayed range.
(() => {
  const ranges = {
    '5m': { step: 1, capacity: 300 },
    '1h': { step: 15, capacity: 240 },
    '6h': { step: 60, capacity: 360 },
    '24h': { step: 300, capacity: 288 }
  };
  const colors = ['#344e46', '#178453', '#bb581c'];
  const range = document.getElementById('historyRange');
  const status = document.getElementById('history-status');
  const clock = new Intl.DateTimeFormat(undefined, { hour: '2-digit', minute: '2-digit' });
  const points = new Map();
  let charts = [];
  let boot, cursor, request;

  function visible() {
    return document.getElementById('dashboardTab').classList.contains('active') && !document.hidden;
  }

  function createChart(color, series) {
    const dataset = {
      data: [], parsing: false, borderColor: color, backgroundColor: color + '13',
      fill: true, pointRadius: 0, pointHoverRadius: 3, borderWidth: 2
    };
    return new Chart(document.getElementById('history' + series), {
      type: 'line', data: { datasets: [dataset] },
      options: {
        responsive: true, maintainAspectRatio: false, animation: false, normalized: true,
        interaction: { mode: 'index', intersect: false },
        plugins: {
          legend: { display: false },
          tooltip: { callbacks: {
            title: items => clock.format(items[0].parsed.x),
            label: item => item.parsed.y.toFixed(1) + (series ? '%' : ' °C')
          }}
        },
        scales: {
          x: { type: 'linear', grid: { display: false },
            ticks: { maxTicksLimit: 6, callback: value => clock.format(value) } },
          y: { min: 0, max: series ? 100 : undefined, afterFit: axis => { axis.width = 40; },
            ticks: { maxTicksLimit: 4 }, grid: { color: '#e5ece7' } }
        }
      }
    });
  }

  function render(now) {
    const { step, capacity } = ranges[range.value];
    const oldest = (Math.floor(now / step) - capacity + 1) * step;
    // Convert monotonic device uptime to the viewer's wall clock without NTP.
    const offset = Date.now() - now * 1000;
    for (const time of points.keys()) {
      if (time < oldest) points.delete(time);
    }
    const rows = [...points.values()];
    const last = rows.at(-1);
    if (!charts.length) charts = colors.map(createChart);
    charts.forEach((chart, series) => {
      chart.options.scales.x.min = offset + oldest * 1000;
      chart.options.scales.x.max = offset + now * 1000;
      chart.data.datasets[0].data = rows.map(row => ({ x: offset + row[0] * 1000, y: row[series + 1] }));
      const value = last?.[series + 1];
      document.getElementById('reading' + series).textContent =
        value == null ? '—' : Math.round(value) + (series ? '%' : ' °C');
      chart.update('none');
    });
    status.textContent = rows.length ? '' : 'Waiting for the first sample…';
  }

  async function refresh() {
    if (!visible() || request) return;
    const controller = new AbortController();
    request = controller;
    const timeout = setTimeout(() => controller.abort(), 5000);
    const query = new URLSearchParams({ range: range.value });
    if (boot) {
      query.set('boot', boot);
      query.set('since', cursor);
    }
    try {
      const response = await fetch('/history?' + query, { cache: 'no-store', signal: controller.signal });
      if (!response.ok) throw Error('History unavailable');
      const data = await response.json();
      if (request !== controller || !visible()) return;
      if (data.full || boot !== data.boot) points.clear();
      boot = data.boot;
      cursor = data.cursor;
      for (const row of data.points) points.set(row[0], row);
      render(data.now);
    } catch {
      if (request === controller && visible()) status.textContent = 'History unavailable. Retrying…';
    } finally {
      clearTimeout(timeout);
      if (request === controller) request = null;
    }
  }

  function restartPolling() {
    request?.abort();
    request = null;
    if (visible()) refresh();
  }
  range.addEventListener('change', () => {
    points.clear();
    boot = undefined;
    charts.forEach((chart, series) => {
      document.getElementById('reading' + series).textContent = '—';
      chart.data.datasets[0].data = [];
      chart.update('none');
    });
    restartPolling();
  });
  window.addEventListener('tabselected', restartPolling);
  document.addEventListener('visibilitychange', restartPolling);
  window.addEventListener('DOMContentLoaded', refresh);
  setInterval(refresh, 1000);
})();
