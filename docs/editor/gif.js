/* Bounded GIF87a/89a decoder, based on the GIF89a specification.
   No networking/dependencies. Palette, transparency, interlace, disposal 2/3. */
// Dictionary/output buffers have fixed limits; malformed chains never grow them.
function gifLzw(data, min, expected) {
    if (min < 2 || min > 8) throw Error('Unsupported GIF LZW code size.');
    const clear = 1 << min,
        end = clear + 1,
        prefix = new Int16Array(4096),
        suffix = new Uint8Array(4096),
        stack = new Uint8Array(4097),
        out = new Uint8Array(expected);
    for (let i = 0; i < clear; i++) suffix[i] = i;
    let bits = 0,
        size = min + 1,
        next = end + 1,
        old = -1,
        first = 0,
        pos = 0,
        ended = false;
    while (bits + size <= data.length * 8) {
        let code = 0;
        for (let b = 0; b < size; b++) code |= ((data[(bits + b) >> 3] >> ((bits + b) & 7)) & 1) << b;
        bits += size;
        if (code === clear) {
            size = min + 1;
            next = end + 1;
            old = -1;
            continue;
        }
        if (code === end) {
            ended = true;
            break;
        }
        const incoming = code;
        let top = 0;
        if (code >= next) {
            if (code !== next || old < 0) throw Error('Invalid GIF LZW dictionary.');
            stack[top++] = first;
            code = old;
        }
        while (code >= clear) {
            if (code >= next || top >= 4096) throw Error('Invalid GIF LZW chain.');
            stack[top++] = suffix[code];
            code = prefix[code];
        }
        first = suffix[code];
        stack[top++] = first;
        if (pos + top > expected) throw Error('GIF has too many pixels.');
        while (top) out[pos++] = stack[--top];
        if (old >= 0 && next < 4096) {
            prefix[next] = old;
            suffix[next++] = first;
            if (next === (1 << size) && size < 12) size++;
        }
        old = incoming;
    }
    if (!ended || pos !== expected) throw Error('Truncated GIF pixel stream.');
    return out;
}

function decodeGif(buffer, frameTransform = null) {
    const data = new Uint8Array(buffer);
    let pos = 0;

    function take(n) {
        if (n < 0 || n > data.length - pos) throw Error('Truncated GIF.');
        const v = data.subarray(pos, pos + n);
        pos += n;
        return v;
    }
    const byte = () => take(1)[0],
        word = () => {
            const a = take(2);
            return a[0] | a[1] << 8;
        };

    function blocks() {
        const parts = [];
        let n, total = 0;
        while ((n = byte())) {
            const p = take(n);
            parts.push(p);
            total += n;
        }
        const out = new Uint8Array(total);
        let o = 0;
        for (const p of parts) {
            out.set(p, o);
            o += p.length;
        }
        return out;
    }
    const header = String.fromCharCode(...take(6));
    if (!['GIF87a', 'GIF89a'].includes(header)) throw Error('Choose a valid GIF.');
    const width = word(),
        height = word(),
        packed = byte(),
        background = byte();
    byte();
    if (!width || !height || width > 1024 || height > 1024) throw Error('GIF dimensions must be at most 1024 × 1024.');
    const global = (packed & 128) ? take(3 * (1 << ((packed & 7) + 1))) : null;
    const rgba = new Uint8ClampedArray(width * height * 4),
        frames = [];
    let control = {
            delay: 100,
            dispose: 0,
            transparent: -1
        },
        previous = null,
        total = 0;

    function clearRect(rect, color) {
        for (let y = rect.y; y < rect.y + rect.h; y++)
            for (let x = rect.x; x < rect.x + rect.w; x++) {
                const o = (y * width + x) * 4;
                rgba.set(color, o);
            }
    }
    let trailer = false;
    for (;;) {
        const type = byte();
        if (type === 59) {
            trailer = true;
            break;
        }
        if (type === 33) {
            const label = byte();
            if (label === 249) {
                if (byte() !== 4) throw Error('Invalid GIF control block.');
                const flags = byte(),
                    delay = word() * 10,
                    index = byte();
                if (byte() !== 0 || (flags >> 2 & 7) > 3) throw Error('Unsupported GIF disposal.');
                control = {
                    delay: delay || 100,
                    dispose: flags >> 2 & 7,
                    transparent: flags & 1 ? index : -1
                };
            } else {
                blocks();
                if (label === 1) throw Error('GIF text frames are unsupported.');
            }
            continue;
        }
        if (type !== 44) throw Error('Invalid GIF block.');
        if (frames.length >= 128 || (frames.length + 1) * width * height > 64000000) throw Error('GIF exceeds 128 source frames or 64 million decoded pixels.');
        if (previous) {
            if (previous.dispose === 2) clearRect(previous, previous.color);
            if (previous.dispose === 3) rgba.set(previous.saved);
        }
        const x = word(),
            y = word(),
            w = word(),
            h = word(),
            flags = byte();
        if (!w || !h || x + w > width || y + h > height) throw Error('GIF frame is outside its canvas.');
        const palette = flags & 128 ? take(3 * (1 << ((flags & 7) + 1))) : global;
        if (!palette) throw Error('Missing GIF palette.');
        const bg = global && background * 3 + 2 < global.length ? [global[background * 3], global[background * 3 + 1], global[background * 3 + 2], 255] : [0, 0, 0, 0];
        const color = control.transparent >= 0 ? [0, 0, 0, 0] : bg;
        if (!frames.length) clearRect({
            x: 0,
            y: 0,
            w: width,
            h: height
        }, color);
        const saved = control.dispose === 3 ? rgba.slice() : null;
        const min = byte(),
            pixels = gifLzw(blocks(), min, w * h);
        // Interlace transmits four row passes; map stream rows back to display rows.
        const rows = [];
        if (flags & 64) {
            for (const [start, step] of [
                    [0, 8],
                    [4, 8],
                    [2, 4],
                    [1, 2]
                ])
                for (let r = start; r < h; r += step) rows.push(r);
        } else
            for (let r = 0; r < h; r++) rows.push(r);
        let i = 0;
        for (const row of rows)
            for (let col = 0; col < w; col++) {
                const index = pixels[i++];
                if (index === control.transparent) continue;
                if (index * 3 + 2 >= palette.length) throw Error('Invalid GIF palette index.');
                const o = ((y + row) * width + x + col) * 4;
                rgba[o] = palette[index * 3];
                rgba[o + 1] = palette[index * 3 + 1];
                rgba[o + 2] = palette[index * 3 + 2];
                rgba[o + 3] = 255;
            }
        total += control.delay;
        if (total > 30000) throw Error('GIF loop must be at most 30 seconds.');
        frames.push({
            rgba: frameTransform ? frameTransform(rgba, width, height) : rgba.slice(),
            duration: control.delay
        });
        previous = {
            x,
            y,
            w,
            h,
            dispose: control.dispose,
            saved,
            color
        };
        control = {
            delay: 100,
            dispose: 0,
            transparent: -1
        };
    }
    if (!trailer || !frames.length) throw Error('GIF has no complete frames.');
    return {
        width,
        height,
        frames,
        total
    };
}
// Sampling keeps runtime frames bounded. The original GIF never reaches MiSTer.
async function normaliseGif(buffer, addLabel, label) {
    const outputSize = typeof ICON_SIZE === "undefined" ? 72 : ICON_SIZE;
    // Composite at source resolution for correct disposal; retain only device-size
    // snapshots. A 1024px source no longer retains megabytes per frame.
    const sourceCanvas = document.createElement('canvas'),
        snapshotCanvas = document.createElement('canvas');
    snapshotCanvas.width = snapshotCanvas.height = outputSize;
    const sourceContext = sourceCanvas.getContext('2d'),
        snapshotContext = snapshotCanvas.getContext('2d');
    snapshotContext.imageSmoothingEnabled = false;
    const gif = decodeGif(buffer, (rgba, width, height) => {
        if (sourceCanvas.width !== width) sourceCanvas.width = width;
        if (sourceCanvas.height !== height) sourceCanvas.height = height;
        const frame = sourceContext.createImageData(width, height);
        frame.data.set(rgba);
        sourceContext.putImageData(frame, 0, 0);
        snapshotContext.clearRect(0, 0, outputSize, outputSize);
        const size = Math.min(width, height);
        snapshotContext.drawImage(sourceCanvas, (width-size)/2, (height-size)/2,
                                  size, size, 0, 0, outputSize, outputSize);
        return snapshotContext.getImageData(0, 0, outputSize, outputSize).data;
    }),
        step = Math.max(200, Math.ceil(gif.total / 32)),
        selected = [];
    let source = 0,
        until = gif.frames[0].duration;
    for (let t = 0; t < gif.total; t += step) {
        while (t >= until && source < gif.frames.length - 1) until += gif.frames[++source].duration;
        const duration = Math.min(step, gif.total - t);
        if (selected.length && selected.at(-1).source === source) selected.at(-1).duration += duration;
        else selected.push({
            source,
            duration
        });
    }
    // Merge a short tail into its predecessor so every exported frame is >=200ms.
    if (selected.length > 1 && selected.at(-1).duration < 200) {
        const tail = selected.pop();
        selected.at(-1).duration += tail.duration;
    }
    const normal = [],
        device = [],
        durations = [];
    const c = document.createElement('canvas');
    c.width = c.height = outputSize;
    const ctx = c.getContext('2d');
    for (const f of selected) {
        const image = ctx.createImageData(outputSize, outputSize);
        image.data.set(gif.frames[f.source].rgba);
        ctx.putImageData(image, 0, 0);
        const icon = renderIcon(c, addLabel, label, true);
        normal.push(icon.normal);
        device.push(icon.device);
        durations.push(Math.max(200, f.duration));
        if (normal.length % 8 === 0) await new Promise(resolve => setTimeout(resolve, 0));
    }
    return {
        frames: normal,
        device,
        durations
    };
}

function animationBytes(frames, durations) {
    if (frames.length < 1 || frames.length > 32 || frames.length !== durations.length) throw Error('Invalid animation frame count.');
    const parts = frames.map(bytes),
        size = 8 + parts.reduce((n, p) => n + 8 + p.length, 0);
    if (size > 524288) throw Error('Animation exceeds 512 KiB.');
    const out = new Uint8Array(size),
        v = new DataView(out.buffer);
    out.set([83, 68, 65, 78]);
    v.setUint16(4, 1, true);
    v.setUint16(6, parts.length, true);
    let offset = 8;
    parts.forEach((p, i) => {
        v.setUint32(offset, durations[i], true);
        v.setUint32(offset + 4, p.length, true);
        out.set(p, offset + 8);
        offset += 8 + p.length;
    });
    return out;
}
