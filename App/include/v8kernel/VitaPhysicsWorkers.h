#pragma once

namespace RBX {
// Per-process capability policy. The original caller and generic scheduler
// remain on application cores; only an optional claim helper may use CPU3.
void setVitaCpu3PhysicsWorkerEnabled(bool enabled);
int getVitaPhysicsConcurrency();
}
