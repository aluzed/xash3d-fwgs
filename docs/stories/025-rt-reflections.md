# 025 - RT shadows, AO and reflections

## Tasks
- [ ] Ray-traced shadows for dynamic lights and sun (`light_environment`).
- [ ] Ray-traced ambient occlusion.
- [ ] Reflections on water, glass and materials flagged reflective (022).
- [ ] Studio models receive world shadows and lighting consistent with
      lightmaps.

## Competitive fairness (multiplayer)
RT reflections and shadows can reveal players that GoldSrc would never show
(around corners, through reflective floors). In multiplayer:
- [ ] Player models are excluded from reflection/shadow rays unless they are
      visible to the viewer through the normal PVS / server-sent entities
      (the client never knows more than the server sends, but rendering must
      not amplify it).
- [ ] Server-side opt-out: honour a server cvar / `ServerInfo` key to force
      `classic` in competitive servers.

## Acceptance criteria
- Visible improvement, toggleable per feature, denoised with 027.

## Dependencies
024, 027.
