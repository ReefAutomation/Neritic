import {
  forwardRef,
  useEffect,
  useImperativeHandle,
  useRef,
} from "preact/compat";
/**
 * Floating LED Bar Canvas
 * @param {import('preact').Ref<any>} ref
 */
export const LedBar = forwardRef(function LedBar(_unused, ref) {
  // Internal buffer ref
  const bufferRef = useRef(null);
  const canvasRef = useRef(null);

  // Expose updateBuffer method via ref
  useImperativeHandle(ref, () => ({
    updateBuffer: (newBuffer) => {
      bufferRef.current = newBuffer;
      drawBar();
    },
  }));

  // Draw the LED bar
  function drawBar() {
    if (!canvasRef.current || !bufferRef.current) return;
    const canvas = canvasRef.current;
    const arr = new Uint8Array(bufferRef.current);
    const ledCount = Math.floor(arr.length / 4);
    const w = canvas.width;
    const h = canvas.height;
    const ctx = canvas.getContext("2d");
    ctx.clearRect(0, 0, w, h);
    if (ledCount === 0) return;
    const ledWidth = w / ledCount;
    for (let i = 0; i < ledCount; ++i) {
      let r1 = arr[i * 4];
      let g1 = arr[i * 4 + 1];
      let b1 = arr[i * 4 + 2];
      let wch1 = arr[i * 4 + 3];
      // Blend white channel with RGB, clamp to 255
      r1 = Math.min(255, r1 + wch1);
      g1 = Math.min(255, g1 + wch1);
      b1 = Math.min(255, b1 + wch1);
      let r2 = r1,
        g2 = g1,
        b2 = b1;
      if (i < ledCount - 1) {
        r2 = arr[(i + 1) * 4];
        g2 = arr[(i + 1) * 4 + 1];
        b2 = arr[(i + 1) * 4 + 2];
        let wch2 = arr[(i + 1) * 4 + 3];
        r2 = Math.min(255, r2 + wch2);
        g2 = Math.min(255, g2 + wch2);
        b2 = Math.min(255, b2 + wch2);
      }
      let x0 = i * ledWidth;
      let x1 = (i + 1) * ledWidth;
      let grad = ctx.createLinearGradient(x0, 0, x1, 0);
      grad.addColorStop(0, `rgb(${r1},${g1},${b1})`);
      grad.addColorStop(1, `rgb(${r2},${g2},${b2})`);
      ctx.fillStyle = grad;
      ctx.fillRect(x0, 0, Math.ceil(ledWidth), h);
    }
    ctx.strokeStyle = "#444";
    ctx.lineWidth = 1;
    ctx.strokeRect(0, 0, w, h);
  }

  // Redraw on resize
  useEffect(() => {
    window.addEventListener("resize", drawBar);
    return () => window.removeEventListener("resize", drawBar);
  }, []);

  // Optionally, clear on unmount
  useEffect(
    () => () => {
      bufferRef.current = null;
    },
    [],
  );

  // Initial mount: clear canvas
  useEffect(() => {
    drawBar();
  }, []);

  return (
    <div id="ledBarContainer" className="led-bar-floating">
      <canvas
        ref={canvasRef}
        id="ledBarCanvas"
        width={1200}
        height={32}
        style={{
          background: "#222",
          borderRadius: "8px",
          flex: 1,
          width: "100%",
          height: "32px",
          display: "block",
        }}
      ></canvas>
    </div>
  );
});
