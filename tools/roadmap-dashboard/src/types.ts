export type Status =
  | "shipped"
  | "in-progress"
  | "specced"
  | "planned"
  | "tentative"
  | "blocked";

export type ItemKind = "version" | "point" | "feature" | "task";

export type Tag = "keystone" | "gate" | "conditional" | "continuous";

export interface RoadmapItem {
  id: string;
  title: string;
  status: Status;
  kind: ItemKind;
  summary?: string;
  notes?: string;
  tags?: Tag[];
  shipped?: string;        // ISO date
  firmness?: "firm" | "tentative";
  specLinks?: string[];
  progressOverride?: number; // 0..100
  children?: RoadmapItem[];
}

export interface Thread {
  id: string;
  title: string;
  summary?: string;
}

export interface RoadmapMeta {
  product: string;
  tagline: string;
  currentVersion: string;
  nextStep: string;
  lastUpdated: string;     // ISO date
  schemaVersion: number;
}

export interface RoadmapDoc {
  meta: RoadmapMeta;
  items: RoadmapItem[];
  continuousThreads: Thread[];
}

export const SCHEMA_VERSION = 1;
