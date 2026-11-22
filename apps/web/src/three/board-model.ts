// Original procedural model of an ESP32-S3-DevKitC-1 (N16R8). Built from
// public dimensions (see src/lib/board-parts.ts); not derived from any CAD
// file. Scene units are millimetres scaled by 0.1 (1 unit = 10 mm).
import {
  BoxGeometry,
  CanvasTexture,
  Color,
  CylinderGeometry,
  Group,
  Mesh,
  MeshPhysicalMaterial,
  MeshStandardMaterial,
  PointLight,
  SphereGeometry,
  SRGBColorSpace,
  type Material,
  type Object3D,
} from "three";
import { BOARD_DIMENSIONS_MM, type BoardPartId } from "@/lib/board-parts";

const S = 0.1;
const L = BOARD_DIMENSIONS_MM.length * S;
const W = BOARD_DIMENSIONS_MM.width * S;
const T = BOARD_DIMENSIONS_MM.thickness * S;

export interface BoardModel {
  root: Group;
  pickables: Object3D[];
  led: { mesh: Mesh; material: MeshPhysicalMaterial; light: PointLight };
  highlight(part: BoardPartId | null): void;
  dispose(): void;
}

function silkscreenTexture(): CanvasTexture | null {
  if (typeof document === "undefined") return null;
  const canvas = document.createElement("canvas");
  canvas.width = 1024;
  canvas.height = 412;
  const ctx = canvas.getContext("2d");
  if (!ctx) return null;
  ctx.fillStyle = "#14181d";
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  // Subtle copper pour texture.
  ctx.strokeStyle = "rgba(255,255,255,0.025)";
  for (let x = -412; x < 1024; x += 14) {
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.lineTo(x + 412, 412);
    ctx.stroke();
  }
  ctx.fillStyle = "rgba(235,238,242,0.85)";
  ctx.font = "600 30px ui-monospace, Menlo, monospace";
  ctx.fillText("ESP32-S3-DevKitC-1", 540, 180);
  ctx.font = "500 22px ui-monospace, Menlo, monospace";
  ctx.fillText("N16R8 · SENTINEL-LAB", 540, 220);
  ctx.fillText("RGB@48", 330, 300);
  ctx.font = "500 18px ui-monospace, Menlo, monospace";
  ctx.fillText("USB", 940, 120);
  ctx.fillText("UART", 935, 300);
  ctx.fillText("BOOT", 860, 60);
  ctx.fillText("RST", 860, 372);
  // Pad outlines along both long edges.
  ctx.strokeStyle = "rgba(235,238,242,0.35)";
  ctx.lineWidth = 2;
  for (let i = 0; i < 22; i += 1) {
    const x = 70 + i * 40.5;
    ctx.strokeRect(x - 13, 10, 26, 26);
    ctx.strokeRect(x - 13, 376, 26, 26);
  }
  const texture = new CanvasTexture(canvas);
  texture.colorSpace = SRGBColorSpace;
  texture.anisotropy = 4;
  return texture;
}

export function createBoardModel(): BoardModel {
  const root = new Group();
  root.name = "esp32-s3-devkitc-1";
  const disposables: { dispose(): void }[] = [];
  const pickables: Object3D[] = [];
  const partMeshes = new Map<BoardPartId, Mesh[]>();

  const track = <T extends { dispose(): void }>(item: T): T => {
    disposables.push(item);
    return item;
  };
  const addPart = (mesh: Mesh, part: BoardPartId | null) => {
    mesh.castShadow = true;
    mesh.receiveShadow = true;
    if (part) {
      mesh.userData.part = part;
      pickables.push(mesh);
      const list = partMeshes.get(part) ?? [];
      list.push(mesh);
      partMeshes.set(part, list);
    }
    root.add(mesh);
    return mesh;
  };
  const box = (w: number, h: number, d: number) => track(new BoxGeometry(w, h, d));

  // Materials
  const silk = silkscreenTexture();
  if (silk) track(silk);
  const pcbTop = track(new MeshStandardMaterial({ color: silk ? 0xffffff : 0x14181d, map: silk, roughness: 0.62, metalness: 0.08 }));
  const pcbSide = track(new MeshStandardMaterial({ color: 0x0c0f12, roughness: 0.8 }));
  const shield = track(new MeshStandardMaterial({ color: 0xcfd3d8, roughness: 0.28, metalness: 1 }));
  const moduleBase = track(new MeshStandardMaterial({ color: 0x1d2430, roughness: 0.55, metalness: 0.1 }));
  const copper = track(new MeshStandardMaterial({ color: 0xd8a04a, roughness: 0.35, metalness: 1 }));
  const gold = track(new MeshStandardMaterial({ color: 0xe2b85c, roughness: 0.25, metalness: 1 }));
  const steel = track(new MeshStandardMaterial({ color: 0xb9bec5, roughness: 0.22, metalness: 1 }));
  const plastic = track(new MeshStandardMaterial({ color: 0x0b0d10, roughness: 0.7 }));
  const chip = track(new MeshStandardMaterial({ color: 0x15181c, roughness: 0.45, metalness: 0.15 }));
  const buttonCap = track(new MeshStandardMaterial({ color: 0x23272d, roughness: 0.5 }));

  // PCB: top face textured, other faces dark.
  const pcbMaterials: Material[] = [pcbSide, pcbSide, pcbTop, pcbSide, pcbSide, pcbSide];
  const pcb = new Mesh(box(L, T, W), pcbMaterials);
  pcb.name = "pcb";
  addPart(pcb, null);

  const top = T / 2;
  // ESP32-S3-WROOM-1 module at the left end (antenna overhanging slightly).
  const modLen = 25.5 * S;
  const modWid = 18 * S;
  const modX = -L / 2 + modLen / 2 - 1.2 * S;
  const base = new Mesh(box(modLen, 0.8 * S, modWid), moduleBase);
  base.position.set(modX, top + 0.4 * S, 0);
  addPart(base, "antenna");
  const can = new Mesh(box(17.6 * S, 2.3 * S, 15.8 * S), shield);
  can.position.set(modX + (modLen - 17.6 * S) / 2 - 0.6 * S, top + 0.8 * S + 1.15 * S, 0);
  addPart(can, "module");
  // Antenna meander traces on the module PCB end.
  for (let i = 0; i < 6; i += 1) {
    const trace = new Mesh(box(0.5 * S, 0.06 * S, 14 * S), copper);
    trace.position.set(modX - modLen / 2 + 1.2 * S + i * 0.95 * S, top + 0.83 * S, 0);
    addPart(trace, "antenna");
  }

  // RGB LED (WS2812, 5 × 5 mm) next to the module.
  const ledPkg = new Mesh(box(5 * S, 1.4 * S, 5 * S), track(new MeshStandardMaterial({ color: 0xf2f2ee, roughness: 0.4 })));
  ledPkg.position.set(modX + modLen / 2 + 4 * S, top + 0.7 * S, -6.5 * S);
  addPart(ledPkg, "rgb");
  const ledMaterial = track(new MeshPhysicalMaterial({ color: 0x2a2f36, emissive: new Color(0x000000), emissiveIntensity: 1, roughness: 0.15, transmission: 0.2, thickness: 0.2 }));
  const ledMesh = new Mesh(track(new SphereGeometry(1.7 * S, 24, 16)), ledMaterial);
  ledMesh.position.set(ledPkg.position.x, top + 1.5 * S, ledPkg.position.z);
  addPart(ledMesh, "rgb");
  const ledLight = new PointLight(0x000000, 0, 3.5, 2);
  ledLight.position.set(ledPkg.position.x, top + 3 * S, ledPkg.position.z);
  root.add(ledLight);

  // USB-C receptacles at the right end (USB above, UART below in plan view).
  const usb = (z: number, part: BoardPartId) => {
    const shell = new Mesh(box(7.4 * S, 3.2 * S, 9 * S), steel);
    shell.position.set(L / 2 - 3.2 * S, top + 1.6 * S, z);
    addPart(shell, part);
    const mouth = new Mesh(box(0.4 * S, 2.2 * S, 7.6 * S), plastic);
    mouth.position.set(L / 2 + 0.45 * S, top + 1.6 * S, z);
    addPart(mouth, part);
  };
  usb(-6.2 * S, "usb");
  usb(6.2 * S, "uart");

  // BOOT / RST tactile buttons.
  const button = (z: number, part: BoardPartId) => {
    const body = new Mesh(box(4.2 * S, 1.6 * S, 3.2 * S), steel);
    body.position.set(L / 2 - 12.5 * S, top + 0.8 * S, z);
    addPart(body, part);
    const cap = new Mesh(track(new CylinderGeometry(0.9 * S, 0.9 * S, 0.8 * S, 20)), buttonCap);
    cap.position.set(body.position.x, top + 2 * S, z);
    addPart(cap, part);
  };
  button(-9.2 * S, "boot");
  button(9.2 * S, "reset");

  // LDO (SOT-223) and USB-serial bridge (QFN).
  const ldo = new Mesh(box(6.5 * S, 1.6 * S, 3.5 * S), chip);
  ldo.position.set(L / 2 - 20 * S, top + 0.8 * S, -4.5 * S);
  addPart(ldo, "ldo");
  const tab = new Mesh(box(3 * S, 0.3 * S, 1.2 * S), steel);
  tab.position.set(ldo.position.x, top + 0.15 * S, ldo.position.z - 2.4 * S);
  addPart(tab, "ldo");
  const bridge = new Mesh(box(4 * S, 0.9 * S, 4 * S), chip);
  bridge.position.set(L / 2 - 20 * S, top + 0.45 * S, 4.5 * S);
  addPart(bridge, "bridge");

  // Small passives scattered between parts (decorative, non-interactive).
  const passive = box(1.6 * S, 0.5 * S, 0.8 * S);
  const passiveMat = track(new MeshStandardMaterial({ color: 0x6b5a45, roughness: 0.6 }));
  const seeds = [[-2, -7], [1, 7], [4, -2], [8, 5], [10, -8], [13, 1], [16, -6], [-4, 3]] as const;
  for (const [x, z] of seeds) {
    const p = new Mesh(passive, passiveMat);
    p.position.set(x * S + 0.6, top + 0.25 * S, z * S);
    addPart(p, null);
  }

  // 2 × 22 male headers along the long edges, pins pointing down.
  const pinGeo = box(0.64 * S, 11 * S, 0.64 * S);
  const housingGeo = box(55.9 * S, 2.5 * S, 2.5 * S);
  for (const z of [-11.43 * S, 11.43 * S]) {
    const housing = new Mesh(housingGeo, plastic);
    housing.position.set(0.6 * S, -T / 2 - 1.25 * S, z);
    addPart(housing, "headers");
    for (let i = 0; i < 22; i += 1) {
      const pin = new Mesh(pinGeo, gold);
      pin.position.set(-26.67 * S + i * 2.54 * S + 0.6 * S, -T / 2 - 4 * S, z);
      addPart(pin, "headers");
    }
  }

  const emissiveDefaults = new Map<Mesh, Color>();
  const highlight = (part: BoardPartId | null) => {
    for (const [id, meshes] of partMeshes) {
      for (const mesh of meshes) {
        const material = mesh.material as MeshStandardMaterial;
        if (!("emissive" in material) || mesh === ledMesh) continue;
        if (!emissiveDefaults.has(mesh)) emissiveDefaults.set(mesh, material.emissive.clone());
        // Materials are shared; tint via a per-mesh clone only when needed.
        if (id === part) {
          if (!mesh.userData.ownMaterial) {
            mesh.material = material.clone();
            mesh.userData.ownMaterial = true;
            track(mesh.material as Material);
          }
          (mesh.material as MeshStandardMaterial).emissive.set(0x1f6feb);
          (mesh.material as MeshStandardMaterial).emissiveIntensity = 0.35;
        } else if (mesh.userData.ownMaterial) {
          (mesh.material as MeshStandardMaterial).emissive.set(0x000000);
          (mesh.material as MeshStandardMaterial).emissiveIntensity = 1;
        }
      }
    }
  };

  return {
    root,
    pickables,
    led: { mesh: ledMesh, material: ledMaterial, light: ledLight },
    highlight,
    dispose() {
      for (const d of disposables) d.dispose();
    },
  };
}
