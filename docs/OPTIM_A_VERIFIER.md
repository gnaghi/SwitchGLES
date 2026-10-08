# Optimisations GFXBench — vérifications à faire sur console

Campagne issue de `gfxbench/docs/PROMPT_SWITCHGLES_COMPLET.md`. Les pistes sont implémentées une par une
sans console ; tout est mesuré et validé à la fin. Chaque piste doit rester réversible seule.

## Mesure de référence (avant)

À faire sur la révision **sans** les pistes ci-dessous (base `eaaee0f` + patch « scan EBO sauté » + `SGL_PERF`),
dans `D:\projets\programmation\switch\DekoGL\gfxbench` :

- [ ] `./build.sh` (lib retail `-O2`, sans `SGL_DEBUG`)
- [ ] À l'écran : `nxlink -a 192.168.1.103 -s gfxbench_<backend>_onscreen.nro -- gl_egypt` puis `gl_trex`
      (FPS, CPU par frame)
- [ ] Hors écran : `tools/run_campaign.sh 30000 gl_trex_off gl_egypt_off gl_trex_off@960x540 gl_egypt_off@960x540`
- [ ] Ligne `[PERF]` (build `-DSGL_PERF_STATS`) : `flush_sync`, `submit_reset`, `frame_start`, draws par frame
- [ ] Tests `driver`, `alu`, `fill`, `blending` (ne doivent pas régresser ensuite)

## Piste A2 — plus de submit + WaitIdle au début de `glClear`

Changement : `dk_clear.c` (flush retiré) ; liste différée des VBO orphelins marquée par slot et vidée dans
`dk_wait_fence` (`dk_command.c`, `dk_buffer.c`, `dk_backend.h`, capacité 64 → 256).

Performance :
- [ ] `flush_sync` et `submit_reset` en cours de frame ≈ 0 sur Egypt et T-Rex (sinon chercher quel seuil
      se déclenche)
- [ ] `frame_start` (attente fence) en hausse attendue : GPU devenu le facteur limitant
- [ ] FPS Egypt à l'écran et hors écran, comparés à la référence et à Nouveau (69,5 FPS)

Budgets par frame (une frame entière doit maintenant tenir dans un seul segment) :
- [ ] Uniforms : 5 Mo/slot (`SGL_UNIFORM_BUF_SIZE / 3`). Un dépassement relance `dk_submit_and_reset` sans
      erreur visible (`dk_state.c` seuil 128 Ko). Relever `diag_uniform_overflows` et l'offset uniform max par
      frame ; agrandir `SGL_UNIFORM_BUF_SIZE` si besoin (mémoire data disponible)
- [ ] Mémoire de commandes : 4 Mo/slot (`SGL_CMD_MEM_SIZE`) ; un dépassement passe par `cbAddMem` (WaitIdle)
- [ ] Client arrays : ~16 Mo/slot

Rendu :
- [ ] `-- --freeze 10000 gl_egypt` comparé à la capture Nouveau (plafond, pièce du fond, chevalier)
- [ ] Idem `gl_trex` (bug A1 préexistant : vérifier seulement qu'il n'y a pas de nouvelle dégradation)

Conformité dEQP-GLES2 (VK-GL-CTS, jamais `validation_test`) :
- [ ] `functional.color_clear.*`, `functional.depth_stencil_clear.*`
- [ ] `functional.fbo.*` (render.color_clear, shared_colorbuffer_clear, depth, stencil, no_rebind, recreate)
- [ ] `functional.fragment_ops.*` (depth, stencil)
- [ ] `functional.flush_finish.*` (accumulation sans swap)
- [ ] `functional.draw.*`, `functional.buffer.*` (orphaning), `functional.read_pixels.*`
- [ ] `functional.clipping.*`, `functional.depth_range.*`, `functional.lifetime.*`
- [ ] Sous-ensemble `functional.texture.*` rendu-vers-texture
- [ ] Régression complète (`regress_oct06.txt`, 16 815 tests) en A/B contre la base d'octobre

Autres :
- [ ] Spearmint : chemin `glBufferData(NULL)` (liste différée par slot), pas de fuite VBO sur une longue
      partie, pas d'artefact

## Piste A3 — `glFlush` asynchrone

Changement : `dk_flush` (`dk_command.c`) = `dkCmdBufFinishList` + `dkQueueSubmitCommands` + `dkQueueFlush`, sans
WaitIdle, sans `dkCmdBufClear`, sans remise à zéro des allocateurs ; `cmdbuf_submitted` et `draws_since_flush`
inchangés. Après `eglSwapBuffers` (`cmdbuf_submitted`), `glFlush` ne fait plus rien (avant : WaitIdle).
`glFinish` inchangé. Point nouveau sur matériel : enregistrer à la suite d'une liste soumise sans `Clear`
(vérifié seulement dans les sources de deko3d).

Mesures en A/B (A2 seul contre A2 + A3) :
- [ ] Hors écran (seul endroit où GFXBench appelle `glFlush`, 99 frames sur 100) : `run_campaign.sh` Egypt et
      T-Rex, 1080p et 540p. Attendu : temps `glFlush` de plusieurs ms à quelques µs, `frame_start` en hausse,
      FPS Egypt hors écran en hausse (estimation non mesurée : +20 à 25 %)
- [ ] `submit_reset` dans `[PERF]` ne compte plus que les seuils (4000 draws, allocateurs) : relever ce nombre
      pour la piste C5
- [ ] À l'écran : FPS inchangés attendus (pas de `glFlush`), pas de régression `driver`/`alu`/`fill`/`blending`

Rendu :
- [ ] Mosaïque hors écran correcte (le GPU recouvre maintenant la frame suivante : surveiller les écritures CPU
      en place dans des buffers encore lus, `glBufferSubData`)
- [ ] `--freeze 10000 gl_egypt` à l'écran identique à Nouveau

Conformité dEQP-GLES2 :
- [ ] `functional.flush_finish.*` : attendu `flush` CompatibilityWarning → Pass, les autres inchangés, aucun crash
      (calibration jusqu'à 2^20 draws : les seuils doivent tenir)
- [ ] `functional.read_pixels.*`, `functional.fbo.*` (render, no_rebind, recreate), `functional.buffer.*`
      (orphaning, sub_data), `functional.lifetime.*`, sous-ensemble `functional.texture.*` (uploads après
      `glFlush`), `functional.color_clear.*`, `functional.depth_stencil_clear.*`, `functional.draw.*`
- [ ] Régression complète en A/B

Risque à surveiller : mémoire de contrôle deko3d (tas, ~30-300 o par `glFlush`) rendue seulement au prochain
`dkCmdBufClear`. Si une application fait des milliers de `glFlush` sans swap ni seuil atteint, envisager un
garde-fou (au-delà de N soumissions, repli sur `dk_submit_and_reset`).

**Résultat console (8 octobre).** A3 seul plantait `gl_trex_off` (2 runs sur 3, fault sans exception CPU) : le
cmdbuf de 4 Mo se remplissait sur ~18 frames flushées (~1,1 Ko par draw T-Rex) et débordait au milieu d'une
commande, avant le seuil de 4 000 draws. Correctif : `dk_flush` fait un `dk_submit_and_reset` quand un budget
dépasse la moitié (`DK_FLUSH_RESET_DRAWS` = 2 000 draws, ou moitié des uniforms / client arrays du slot).
Validé : 5 tests sur 5, aucun débordement ; `gl_trex_off` 38,2 / 34,2 / 37,6 FPS, `gl_egypt_off` 65,7 FPS
(sans A3 : 34,1 et 62,7 ; avant la campagne : 15,4). Attention : un job lancé après un retour automatique de
sgl_lab plantait au démarrage tant que GFXBench n'appelait pas `sglShutdown()` (corrigé dans gfxbench `80ac916`).
- [ ] dEQP `flush_finish` et passe rapide ci-dessous

## Piste B1 — une seule barrière au changement de FBO

Changement : suppression de `insert_barrier` (appel dans `glBindFramebuffer`, `dk_insert_barrier`, entrée de
`sgl_backend_ops_t`). On garde la barrière de `dk_bind_framebuffer` (Full + Image | Descriptors | L2 | Zcull),
qui couvrait déjà tous les drapeaux de la première et qui sert aussi `glFramebufferTexture2D`,
`glFramebufferRenderbuffer` et les suppressions. Aucune commande n'était enregistrée entre les deux.

Performance (gain attendu faible, quelques dizaines de µs par frame : préalable à B2) :
- [ ] Egypt : 7 changements de FBO par frame (ombre + 2 passes de flou, main, motion blur) → 7 barrières Full
      de moins par frame. `SGL_PERF` ne compte pas les barrières : si l'on veut le vérifier, ajouter un
      compteur (wrapper unique autour des ~20 appels `dkCmdBufBarrier`)
- [ ] FPS Egypt à l'écran et hors écran en A/B (A2+A3 contre A2+A3+B1)

Rendu et conformité :
- [ ] `--freeze 10000 gl_egypt` identique à Nouveau (ombres, flou, motion blur)
- [ ] dEQP `functional.fbo.*` (api, completeness, render.* dont recreate_* et no_rebind, stencil),
      `functional.texture.*` (rendu-vers-texture, cubemaps), `functional.fragment_ops.depth/stencil`,
      `functional.color_clear.*`, `functional.depth_stencil_clear.*`
- [ ] Liste ciblée glmark2 du 4 octobre (`lists/glmark2_regress_full.txt`), puis régression complète

À surveiller (lié à A2, pas à B1) : sans le flush au clear, l'invalidation des descripteurs de texture en
cours de frame ne repose plus que sur le début de frame et les `postSubmitFlush` de deko3d. Vérifier qu'aucun
scintillement de texture ne réapparaît sur spearmint.

## Piste B2 — plus d'invalidation L2 sur les dépendances GPU → GPU (option 1) + correctif compressé

Changement :
- Toutes les barrières passent par `dk_barrier()` (`dk_internal.h`), qui compte par frame les barrières Full
  (`bar_full`) et les invalidations L2 (`bar_l2`) dans la ligne `[PERF]`.
- `L2Cache` retiré (barrière `Full` conservée) : changement de FBO (`dk_framebuffer.c`), `dk_rebind_render_target`,
  clear couleur et clear depth (`dk_clear.c`), premier échantillonnage d'une cible (`dk_bind_texture`), blit
  (`dk_blit_framebuffer`), mipmaps (`dk_generate_mipmap`).
- `L2Cache` conservé : début de frame et resets (`dk_command.c`), `vbo_data_dirty` (`dk_draw.c`), `glReadPixels`,
  sous-image compressée.
- `sampler_dirty[handle]` : posé quand `glTexParameter` ou la suppression d'une texture réécrit un descripteur en
  cours de frame ; consommé dans `dk_bind_texture` par Full + Image | Descriptors | L2 (jamais de barrière dans
  `dk_texture_parameter`).
- Correctif `dk_compressed_texture_sub_image_2d` : la zone temporaire n'est plus rendue juste après la copie
  (elle était réutilisable par le draw suivant avant l'exécution de la copie) ; contrôle de place contre la
  fin du slot.

Vérifications :
- [ ] `[PERF]` Egypt : `bar_l2` ≈ 1 par frame (début de frame) au lieu d'environ 23 ; `bar_full` inchangé
- [ ] Rendu Egypt identique à Nouveau (ombre projective = rendu → échantillonnage, flou = ping-pong)
- [ ] dEQP texture.specification (copyteximage, subimage), texture.mipmap (generate), fbo.render, state_query.texture
- [ ] Spearmint : pas de scintillement de texture, cinématiques correctes (sous-image vidéo)

## Passe rapide de non-régression (A2 + A3 + B1 + B2), préparée le 7 octobre

Liste : `VK-GL-CTS/framework/platform/switch/lists/optim_oct07.txt` (1 479 tests, extraits de `regress_oct06.txt`) :
color_clear, depth_stencil_clear, fbo.api, fbo.render, flush_finish, read_pixels, buffer.write, fragment_ops
(depth, stencil, depth_stencil, scissor), lifetime, draw, texture (specification, mipmap, completeness, wrap),
state_query (texture, fbo, rbo). Copiée dans `lists/current.txt` (ancienne version :
`lists/current_before_optim_oct07.txt`) et compilée dans le NRO (2 lots).

1. dEQP :
   `nxlink -a 192.168.1.103 -s VK-GL-CTS/build-switch/modules/gles2/deqp-gles2.nro | tee VK-GL-CTS/framework/platform/switch/nxlink_output_optim_oct07.txt`
2. Comparaison (dans `VK-GL-CTS/framework/platform/switch`) :
   `python3 compare_results.py nxlink_output_optim_oct07.txt nxlink_output_glmark2_regress.txt ../../../../switchGLES/Alltestresults.txt`
   Attendu : 0 régression, 0 test sans résultat ; `flush_finish.flush` CompatibilityWarning → Pass.
3. GFXBench (rendu + FPS) : `gfxbench_switchgles_onscreen.nro -- --freeze 10000 gl_egypt`, puis `-- gl_egypt`.
