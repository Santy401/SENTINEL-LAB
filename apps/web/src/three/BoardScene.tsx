// three.js scene for the ESP32-S3 board. Loaded lazily as its own chunk so the
// dashboard renders first and works without WebGL.
import { useEffect, useRef } from "react";
import {
  ACESFilmicToneMapping,
  MathUtils,
  Vector3,
  Color,
  DirectionalLight,
  HemisphereLight,
  Mesh,
  MeshBasicMaterial,
  PCFSoftShadowMap,
  PerspectiveCamera,
  PlaneGeometry,
  PMREMGenerator,
  Raycaster,
  RingGeometry,
  Scene,
  ShadowMaterial,
  SRGBColorSpace,
  Vector2,
  WebGLRenderer,
  DoubleSide,
} from "three";
import { OrbitControls } from "three/examples/jsm/controls/OrbitControls.js";
import { RoomEnvironment } from "three/examples/jsm/environments/RoomEnvironment.js";
import { ledLevel, type BoardPartId } from "@/lib/board-parts";
import { createBoardModel } from "./board-model";

export interface BoardSceneProps {
  led: { r: number; g: number; b: number } | null;
  connected: boolean;
  /** Increment to emit one propagation pulse (e.g. on a firmware event). */
  pulseKey: number;
  selected: BoardPartId | null;
  onHover(part: BoardPartId | null): void;
  onSelect(part: BoardPartId | null): void;
  reducedMotion: boolean;
  dark: boolean;
}

interface Live {
  props: BoardSceneProps;
  pulseStart: number;
  lastPulseKey: number;
}

export default function BoardScene(props: BoardSceneProps) {
  const host = useRef<HTMLDivElement>(null);
  const live = useRef<Live>({ props, pulseStart: -1, lastPulseKey: props.pulseKey });
  live.current.props = props;

  useEffect(() => {
    const container = host.current;
    if (!container) return;
    let renderer: WebGLRenderer;
    try {
      renderer = new WebGLRenderer({ antialias: true, alpha: true, powerPreference: "high-performance" });
    } catch {
      container.dataset.webgl = "failed";
      return;
    }
    let pixelRatio = Math.min(window.devicePixelRatio || 1, 2);
    renderer.setPixelRatio(pixelRatio);
    renderer.outputColorSpace = SRGBColorSpace;
    renderer.toneMapping = ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.05;
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = PCFSoftShadowMap;
    renderer.domElement.setAttribute("aria-hidden", "true");
    container.appendChild(renderer.domElement);

    const scene = new Scene();
    const pmrem = new PMREMGenerator(renderer);
    const environment = pmrem.fromScene(new RoomEnvironment(), 0.04).texture;
    scene.environment = environment;

    const camera = new PerspectiveCamera(32, 1, 0.1, 100);
    camera.position.set(4.2, 4.6, 6.2).multiplyScalar(1.3);

    const controls = new OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.08;
    controls.minDistance = 3.2;
    controls.maxDistance = 14;
    controls.maxPolarAngle = Math.PI * 0.62;
    controls.target.set(0, -0.35, 0);
    controls.autoRotate = !props.reducedMotion;
    controls.autoRotateSpeed = 0.55;

    scene.add(new HemisphereLight(0xdfe8ff, 0x1a1d22, 0.9));
    const key = new DirectionalLight(0xffffff, 2.2);
    key.position.set(4, 8, 3);
    key.castShadow = true;
    key.shadow.mapSize.set(1024, 1024);
    key.shadow.radius = 6;
    key.shadow.camera.left = -5;
    key.shadow.camera.right = 5;
    key.shadow.camera.top = 5;
    key.shadow.camera.bottom = -5;
    scene.add(key);
    const rim = new DirectionalLight(0x7cc4ff, 0.8);
    rim.position.set(-6, 3, -5);
    scene.add(rim);

    const floor = new Mesh(new PlaneGeometry(30, 30), new ShadowMaterial({ opacity: 0.22 }));
    floor.rotation.x = -Math.PI / 2;
    floor.position.y = -1.55;
    floor.receiveShadow = true;
    scene.add(floor);

    // Propagation pulse ring around the board.
    const pulseMaterial = new MeshBasicMaterial({ color: 0x38bdf8, transparent: true, opacity: 0, side: DoubleSide, depthWrite: false });
    const pulse = new Mesh(new RingGeometry(0.96, 1, 96), pulseMaterial);
    pulse.rotation.x = -Math.PI / 2;
    pulse.position.y = -1.5;
    scene.add(pulse);

    const model = createBoardModel();
    model.root.rotation.y = -0.35;
    scene.add(model.root);

    const raycaster = new Raycaster();
    const pointer = new Vector2();
    let hovered: BoardPartId | null = null;
    let downAt = { x: 0, y: 0 };
    const pick = (event: PointerEvent): BoardPartId | null => {
      const rect = renderer.domElement.getBoundingClientRect();
      pointer.set(((event.clientX - rect.left) / rect.width) * 2 - 1, -((event.clientY - rect.top) / rect.height) * 2 + 1);
      raycaster.setFromCamera(pointer, camera);
      const hit = raycaster.intersectObjects(model.pickables, false)[0];
      return (hit?.object.userData.part as BoardPartId | undefined) ?? null;
    };
    const onMove = (event: PointerEvent) => {
      const part = pick(event);
      if (part !== hovered) {
        hovered = part;
        renderer.domElement.style.cursor = part ? "pointer" : "grab";
        live.current.props.onHover(part);
      }
    };
    const onDown = (event: PointerEvent) => {
      downAt = { x: event.clientX, y: event.clientY };
      controls.autoRotate = false;
    };
    const onUp = (event: PointerEvent) => {
      // Treat as a click only if the pointer barely moved (not an orbit drag).
      if (Math.hypot(event.clientX - downAt.x, event.clientY - downAt.y) > 6) return;
      live.current.props.onSelect(pick(event));
    };
    renderer.domElement.addEventListener("pointermove", onMove);
    renderer.domElement.addEventListener("pointerdown", onDown);
    renderer.domElement.addEventListener("pointerup", onUp);

    // Frame the whole board for the current aspect ratio (narrow phones need
    // a longer distance), until the user zooms or orbits.
    const viewDir = new Vector3(4.2, 4.6, 6.2).normalize();
    let userMoved = false;
    controls.addEventListener("start", () => {
      userMoved = true;
    });
    const BOARD_SPAN = 8.2;
    const resize = () => {
      const width = container.clientWidth || 1;
      const height = container.clientHeight || 1;
      renderer.setSize(width, height, false);
      camera.aspect = width / height;
      camera.updateProjectionMatrix();
      const vfov = MathUtils.degToRad(camera.fov);
      const hfov = 2 * Math.atan(Math.tan(vfov / 2) * camera.aspect);
      const fit = Math.max((BOARD_SPAN / 2) / Math.tan(hfov / 2), (BOARD_SPAN * 0.42) / Math.tan(vfov / 2));
      controls.maxDistance = Math.max(14, fit * 1.6);
      if (!userMoved) camera.position.copy(viewDir).multiplyScalar(fit).add(controls.target);
    };
    const observer = new ResizeObserver(resize);
    observer.observe(container);
    resize();

    let visible = true;
    const io = new IntersectionObserver(entries => {
      visible = entries.some(e => e.isIntersecting);
    });
    io.observe(container);

    const ledColor = new Color();
    const target = new Color();
    let frame = 0;
    let last = performance.now();
    let slowFrames = 0;
    let lastSelected: BoardPartId | null = null;
    const tick = (now: number) => {
      frame = requestAnimationFrame(tick);
      if (!visible || document.hidden) {
        last = now;
        return;
      }
      const dt = Math.min(0.1, (now - last) / 1000);
      last = now;
      // Adaptive resolution: drop the pixel ratio after sustained slow frames.
      slowFrames = dt > 0.028 ? slowFrames + 1 : Math.max(0, slowFrames - 1);
      if (slowFrames > 45 && pixelRatio > 1) {
        pixelRatio = Math.max(1, pixelRatio - 0.5);
        renderer.setPixelRatio(pixelRatio);
        resize();
        slowFrames = 0;
      }

      const p = live.current.props;
      controls.autoRotate = controls.autoRotate && !p.reducedMotion;
      if (p.selected !== lastSelected) {
        model.highlight(p.selected);
        lastSelected = p.selected;
      }

      // LED mirrors the physical WS2812 with a short ease, never a flash.
      if (p.led) target.setRGB(p.led.r / 255, p.led.g / 255, p.led.b / 255, SRGBColorSpace);
      else target.set(0x000000);
      ledColor.lerp(target, p.reducedMotion ? 1 : Math.min(1, dt * 6));
      const level = ledLevel(p.led);
      model.led.material.emissive.copy(ledColor);
      model.led.material.emissiveIntensity = 0.6 + level * 5;
      model.led.material.color.copy(p.led ? ledColor : new Color(0x2a2f36));
      model.led.light.color.copy(ledColor);
      model.led.light.intensity = level * 1.6;

      if (p.pulseKey !== live.current.lastPulseKey) {
        live.current.lastPulseKey = p.pulseKey;
        live.current.pulseStart = p.reducedMotion ? -1 : now;
      }
      const elapsed = live.current.pulseStart < 0 ? -1 : (now - live.current.pulseStart) / 1600;
      if (elapsed >= 0 && elapsed <= 1) {
        const scale = 1.2 + elapsed * 5.5;
        pulse.scale.set(scale, scale, 1);
        pulseMaterial.opacity = 0.55 * (1 - elapsed);
      } else {
        pulseMaterial.opacity = 0;
      }
      pulseMaterial.color.set(p.connected ? 0x38bdf8 : 0x64748b);
      scene.background = null;
      floor.material.opacity = p.dark ? 0.35 : 0.18;

      controls.update();
      renderer.render(scene, camera);
    };
    frame = requestAnimationFrame(tick);

    return () => {
      cancelAnimationFrame(frame);
      observer.disconnect();
      io.disconnect();
      renderer.domElement.removeEventListener("pointermove", onMove);
      renderer.domElement.removeEventListener("pointerdown", onDown);
      renderer.domElement.removeEventListener("pointerup", onUp);
      controls.dispose();
      model.dispose();
      pulse.geometry.dispose();
      pulseMaterial.dispose();
      floor.geometry.dispose();
      floor.material.dispose();
      environment.dispose();
      pmrem.dispose();
      renderer.dispose();
      renderer.domElement.remove();
    };
    // The scene is built once; live props are read through the ref.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  return <div ref={host} className="board-canvas" />;
}
