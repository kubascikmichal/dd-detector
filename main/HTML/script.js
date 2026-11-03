'use strict';

const POLL_MS = 1000;
const HISTORY_SECONDS = 120;
const METER_MIN_DB = 30;
const METER_MAX_DB = 130;
const MAC_PATTERN = /^[0-9a-f]{2}(:[0-9a-f]{2}){5}$/i;

const $ = (id) => document.getElementById(id);
const setText = (id, value) => { $(id).textContent = value; };
const orDash = (value, suffix = '') => (value === undefined ? '--' : value + suffix);

function setPill(id, kind, label) {
    const pill = $(id);
    pill.className = 'pill ' + kind;
    pill.textContent = label;
}

async function fetchJson(url, options) {
    const response = await fetch(url, options);
    const body = await response.json();
    return { ok: response.ok, body };
}

function formatDuration(seconds) {
    const h = Math.floor(seconds / 3600);
    const m = Math.floor(seconds / 60) % 60;
    const s = String(seconds % 60).padStart(2, '0');
    return h > 0 ? `${h}h ${m}m` : `${m}:${s}`;
}

function formatBytes(value) {
    return value >= 1024 ? (value / 1024).toFixed(1) + ' kB/s' : value.toFixed(1) + ' B/s';
}

/* ---------- Chart ---------- */

const history = []; // { t: ms, db: number|null }; null leaves a gap in the line
let lastStats = null;
let hoverX = null;

function svgEl(name, attrs, text) {
    const el = document.createElementNS('http://www.w3.org/2000/svg', name);
    for (const key in attrs) {
        el.setAttribute(key, attrs[key]);
    }
    if (text !== undefined) {
        el.textContent = text;
    }
    return el;
}

function chartRange(values, threshold) {
    const all = threshold === undefined ? values : values.concat(threshold);
    if (all.length === 0) {
        return { low: 30, high: 90, step: 20 };
    }
    const low = Math.floor((Math.min(...all) - 5) / 10) * 10;
    const high = Math.ceil((Math.max(...all) + 5) / 10) * 10;
    return { low, high, step: high - low > 60 ? 20 : 10 };
}

function linePaths(points, baseline) {
    let line = '';
    let wash = '';
    let run = [];
    const flush = () => {
        if (run.length > 1) {
            const d = run.map((p, i) => `${i ? 'L' : 'M'}${p.x.toFixed(1)} ${p.y.toFixed(1)}`).join('');
            line += d;
            wash += `${d}L${run[run.length - 1].x.toFixed(1)} ${baseline}L${run[0].x.toFixed(1)} ${baseline}Z`;
        }
        run = [];
    };
    for (const point of points) {
        if (point.db === null) {
            flush();
        } else {
            run.push(point);
        }
    }
    flush();
    return { line, wash };
}

function drawChart() {
    const svg = $('chart-svg');
    const width = svg.clientWidth;
    const height = svg.clientHeight;
    if (!width || !height) {
        return;
    }
    svg.setAttribute('viewBox', `0 0 ${width} ${height}`);
    svg.replaceChildren();

    const margin = { top: 18, right: 12, bottom: 22, left: 34 };
    const plotW = width - margin.left - margin.right;
    const plotH = height - margin.top - margin.bottom;
    const now = Date.now();
    const threshold = lastStats ? lastStats.threshold : undefined;
    const values = history.filter((s) => s.db !== null).map((s) => s.db);
    const { low, high, step } = chartRange(values, threshold);
    const xOf = (t) => margin.left + plotW * (1 - (now - t) / (HISTORY_SECONDS * 1000));
    const yOf = (db) => margin.top + plotH * (1 - (db - low) / (high - low));
    const baseline = margin.top + plotH;

    for (let db = low; db <= high; db += step) {
        svg.append(svgEl('line', { class: 'grid', x1: margin.left, x2: width - margin.right, y1: yOf(db), y2: yOf(db) }));
        svg.append(svgEl('text', { x: margin.left - 6, y: yOf(db) + 4, 'text-anchor': 'end' }, db));
    }
    svg.append(svgEl('text', { x: margin.left, y: height - 4 }, '-2 min'));
    svg.append(svgEl('text', { x: width - margin.right, y: height - 4, 'text-anchor': 'end' }, 'now'));

    const points = history.map((s) => ({ ...s, x: xOf(s.t), y: s.db === null ? 0 : yOf(s.db) }));
    const { line, wash } = linePaths(points, baseline);
    svg.append(svgEl('path', { class: 'wash', d: wash }));
    svg.append(svgEl('path', { class: 'series', d: line }));

    if (threshold !== undefined) {
        const y = yOf(threshold);
        svg.append(svgEl('line', { class: 'limit', x1: margin.left, x2: width - margin.right, y1: y, y2: y }));
        svg.append(svgEl('text', { x: margin.left + 4, y: y - 5 }, `Threshold ${threshold} dB`));
    }

    const measured = points.filter((p) => p.db !== null);
    const last = measured[measured.length - 1];
    if (last) {
        svg.append(svgEl('circle', { class: 'dot', cx: last.x, cy: last.y, r: 5 }));
    }
    drawHover(svg, measured, baseline);
}

function drawHover(svg, measured, baseline) {
    const tooltip = $('tooltip');
    if (hoverX === null || measured.length === 0) {
        tooltip.hidden = true;
        return;
    }
    const nearest = measured.reduce((best, p) => (Math.abs(p.x - hoverX) < Math.abs(best.x - hoverX) ? p : best));
    svg.append(svgEl('line', { class: 'cross', x1: nearest.x, x2: nearest.x, y1: 18, y2: baseline }));
    svg.append(svgEl('circle', { class: 'dot', cx: nearest.x, cy: nearest.y, r: 5 }));
    const secondsAgo = Math.round((Date.now() - nearest.t) / 1000);
    tooltip.textContent = `${nearest.db.toFixed(1)} dB · ${secondsAgo ? secondsAgo + ' s ago' : 'now'}`;
    tooltip.hidden = false;
    const maxLeft = svg.clientWidth - tooltip.offsetWidth;
    tooltip.style.left = Math.max(0, Math.min(maxLeft, nearest.x - tooltip.offsetWidth / 2)) + 'px';
}

function addSample(db) {
    const now = Date.now();
    history.push({ t: now, db });
    while (history.length && history[0].t < now - HISTORY_SECONDS * 1000) {
        history.shift();
    }
}

/* ---------- Status rendering ---------- */

function renderLevel(s) {
    const hasValue = s.db !== undefined && s.sensor_ok;
    setText('db', hasValue ? s.db.toFixed(1) : '--');
    const percent = (db) => Math.max(0, Math.min(100, (db - METER_MIN_DB) / (METER_MAX_DB - METER_MIN_DB) * 100));
    $('meter-fill').style.width = (hasValue ? percent(s.db) : 0) + '%';
    const marker = $('meter-threshold');
    marker.hidden = s.threshold === undefined;
    if (s.threshold !== undefined) {
        marker.style.left = percent(s.threshold) + '%';
    }

    if (!s.sensor_ok) {
        setPill('level-status', 'critical', s.samples ? 'Sensor lost' : 'Waiting for sensor');
    } else if (s.threshold === undefined) {
        setPill('level-status', '', 'No threshold set');
    } else if (s.db > s.threshold) {
        setPill('level-status', 'critical', 'Over threshold');
    } else {
        setPill('level-status', 'good', 'Below threshold');
    }
}

function renderGenerator(gen) {
    const views = {
        idle: ['', 'Not connected', 'Press Connect to start the handshake.'],
        sending: ['warning', 'Connecting…', `Sending handshake, attempt ${gen.attempts}.`],
        waiting_reply: ['warning', 'Waiting for data…', 'The generator answered at radio level. Waiting for its data.'],
        connected: ['good', 'Connected', gen.reply_ms !== undefined ? `Handshake took ${gen.reply_ms} ms.` : 'Receiving data from the generator.'],
        no_reply: ['warning', 'No data', 'The generator is reachable but sent no data. Its firmware may need the handshake reply.'],
        no_ack: ['critical', 'Not reachable', `No answer after ${gen.attempts} attempts. Check the MAC address and that the generator is on.`],
    };
    let [kind, label, detail] = views[gen.state] || views.idle;
    if (!gen.configured) {
        [kind, label, detail] = ['', 'No generator set', 'Enter the generator MAC address in Settings.'];
    } else if (gen.link_lost && gen.state !== 'sending') {
        [kind, label, detail] = ['critical', 'Link lost', 'The last frames were not delivered. Try connecting again.'];
    }
    setPill('gen-status', kind, label);
    setText('gen-detail', detail);

    const busy = gen.state === 'sending' || gen.state === 'waiting_reply';
    $('connect').disabled = busy || connecting;
    $('connect').textContent = busy || connecting ? 'Connecting…' : 'Connect to generator';

    setText('tx-rate', formatBytes(gen.tx_bps));
    setText('rx-rate', formatBytes(gen.rx_bps));
    const finished = gen.tx_acked + gen.tx_failed;
    setText('delivery', finished ? `${Math.round(gen.tx_acked / finished * 100)}%` : '--');
    setText('rssi', orDash(gen.rssi, ' dBm'));
    setText('frames', `${gen.tx_frames} / ${gen.rx_frames}`);
    setText('last-rx', gen.last_rx_s === undefined ? 'Never' : `${formatDuration(gen.last_rx_s)} ago`);
    setText('reply', gen.reply || '--');
}

function renderStats(s) {
    setText('min', orDash(s.min, ' dB'));
    setText('avg', orDash(s.avg, ' dB'));
    setText('max', orDash(s.max, ' dB'));
    setText('samples', s.samples);
    setText('over-boot', s.over_boot);
    setText('over-total', s.over_total);
    setText('uptime', formatDuration(s.uptime_s));
    setText('clients', s.clients);
    setText('bad-frames', s.bad_frames);
    setText('heap', Math.round(s.free_heap / 1024) + ' kB');
    setText('mac', s.mac);
}

function renderOffline(message) {
    const banner = $('offline');
    banner.hidden = !message;
    banner.textContent = message || '';
}

/* ---------- Polling ---------- */

let connecting = false;
let portalSeconds = null;

async function poll() {
    try {
        const { ok, body } = await fetchJson('/getStats');
        if (!ok) {
            throw new Error('bad response');
        }
        lastStats = body;
        portalSeconds = body.portal_s;
        renderOffline(null);
        setText('portal', `Wi-Fi portal closes in ${formatDuration(body.portal_s)}`);
        addSample(body.sensor_ok && body.db !== undefined ? body.db : null);
        renderLevel(body);
        renderGenerator(body.gen);
        renderStats(body);
    } catch (e) {
        addSample(null);
        const closed = portalSeconds !== null && portalSeconds <= 5;
        setText('portal', closed ? 'Wi-Fi portal closed' : 'Connection lost');
        renderOffline(closed
            ? 'The Wi-Fi portal has closed and the detector is now in ESP-NOW only mode. Restart the detector to open the portal again.'
            : 'Cannot reach the detector. Retrying…');
    }
    drawChart();
    setTimeout(poll, POLL_MS);
}

/* ---------- Settings and handshake ---------- */

function validateSettings() {
    const threshold = $('threshold').value.trim();
    const mac = $('generator').value.trim();
    const number = Number(threshold);
    const thresholdBad = $('threshold').validity.badInput || (threshold !== '' && !(number >= 0 && number <= 200));
    const macBad = mac !== '' && !MAC_PATTERN.test(mac);
    $('threshold').setAttribute('aria-invalid', thresholdBad);
    $('generator').setAttribute('aria-invalid', macBad);
    if (thresholdBad) {
        return { error: 'Threshold must be between 0 and 200 dB.' };
    }
    if (macBad) {
        return { error: 'MAC address must look like aa:bb:cc:dd:ee:ff.' };
    }
    return { treshold: threshold, MAC: mac };
}

async function saveSettings() {
    const settings = validateSettings();
    if (settings.error) {
        return settings.error;
    }
    try {
        const { ok, body } = await fetchJson('/setSettings', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(settings),
        });
        return ok ? null : body.error;
    } catch (e) {
        return 'Could not reach the detector.';
    }
}

async function connectGenerator() {
    connecting = true;
    $('connect').disabled = true;
    $('connect').textContent = 'Connecting…';
    let started = false;
    // Connect always uses what is on screen, even if Save was not pressed.
    const error = await saveSettings();
    if (error) {
        setText('settings-result', error);
    } else if ($('generator').value.trim() === '') {
        setText('settings-result', 'Enter the generator MAC address first.');
    } else {
        try {
            const { ok, body } = await fetchJson('/connectGenerator', { method: 'POST' });
            started = ok;
            setText('settings-result', ok ? '' : body.error);
        } catch (e) {
            setText('settings-result', 'Could not reach the detector.');
        }
    }
    connecting = false;
    if (!started) { // on success the next poll updates the button from the real state
        $('connect').disabled = false;
        $('connect').textContent = 'Connect to generator';
    }
}

async function loadSettings() {
    try {
        const { body } = await fetchJson('/getSettings');
        $('threshold').value = body.treshold || '';
        $('generator').value = body.MAC || '';
    } catch (e) {
        setTimeout(loadSettings, 2000);
    }
}

$('settings').addEventListener('submit', async (event) => {
    event.preventDefault();
    const error = await saveSettings();
    setText('settings-result', error || 'Settings saved.');
});
$('connect').addEventListener('click', connectGenerator);

const chart = $('chart');
chart.addEventListener('pointermove', (event) => {
    hoverX = event.clientX - chart.getBoundingClientRect().left;
    drawChart();
});
chart.addEventListener('pointerleave', () => {
    hoverX = null;
    drawChart();
});
window.addEventListener('resize', drawChart);

loadSettings();
poll();
