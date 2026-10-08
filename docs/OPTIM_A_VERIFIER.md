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

## Piste B3 — plus de barrière Full après les clears

Changement (`dk_clear.c`) :
- Clear couleur : la barrière Full + Image qui suivait `dkCmdBufClearColorFloat` est supprimée. Un clear est une
  macro `ClearBuffers` du moteur 3D, ordonnée par le matériel avec les draws qui suivent dans la même cible. Les
  autres consommateurs attendent déjà : changement de cible (`dk_bind_framebuffer`, Full), premier échantillonnage
  d'une cible rendue (`dk_bind_texture`, Full + Image), `glReadPixels`, blit, copies, mipmaps (Full + Image avant
  lecture), présentation (fence deko3d avec `FlushCache`).
- Clear depth/stencil : la barrière Full + Image devient `None + Zcull` (même commande `InvalidateZcullNoWfi`,
  plus de vidage du pipeline).
- `DkBarrier_Tiles` n'est plus émise que si le stencil est effacé (`stencilMask != 0`). Elle avait été ajoutée en
  mars comme hypothèse pour les stencil ops ; `docs/conformance_assessment.md` et `deqp_conformance_mar22.md`
  notent qu'elle n'a rien corrigé, et le tiled cache n'est jamais activé (`dkCmdBufTiledCacheOp` jamais appelé).
  Les clears depth seuls reviennent au flux validé avant son ajout (tests depth/clipping du 11 mars).

Performance :
- [ ] `[PERF]` Egypt : `bar_full` baisse d'environ 4 à 5 par frame (un par `glClear`, couleur et profondeur)
- [ ] FPS Egypt et T-Rex à l'écran et hors écran en A/B (B2 contre B2 + B3)

Rendu :
- [ ] `--freeze 10000 gl_egypt` et `gl_trex` identiques à Nouveau (ombres : clear puis rendu dans un FBO puis
      échantillonnage ; flou en ping-pong)
- [ ] Spearmint : pas de traînées entre frames (clear du framebuffer par défaut suivi des draws)

Conformité dEQP-GLES2 :
- [ ] `functional.color_clear.*`, `functional.depth_stencil_clear.*` (toutes les combinaisons masques/scissor)
- [ ] `functional.fbo.render.*` (color_clear, shared_colorbuffer_clear, depth, stencil, stencil_clear,
      recreate_*, resize, no_rebind), `functional.fbo.api.*`
- [ ] `functional.fragment_ops.depth*`, `functional.fragment_ops.stencil*`, `functional.fragment_ops.interaction.*`
      (les 32 stencil ops connus doivent rester les seuls échecs : même flux pour les clears stencil)
- [ ] `functional.read_pixels.*`, `functional.texture.specification.copyteximage*` (lecture juste après un clear)
- [ ] Si tout passe, essai supplémentaire : retirer aussi `DkBarrier_Tiles` pour les clears stencil et relancer
      `fragment_ops.stencil*` + `fbo.render.stencil*` (la barrière n'a aucun effet documenté)

## Piste B4 — Zcull plus invalidé après le clear de profondeur

Changement (`dk_clear.c`) : après `dkCmdBufClearDepthStencil`, plus de barrière `Zcull` et plus de
`dk_rebind_render_target` (qui en remettait une, avec un Full, dans les FBO avec profondeur). Justification
lue dans deko3d (`gpu_3d_base.cpp`) : `dkCmdBufClearDepthStencil` écrit `ZcullClearDepth` (indices
IsLessThanHalf / IsOneOrZero) juste avant `ClearBuffers`, c'est le clear qui initialise le Zcull ; l'invalider
ensuite privait toute la passe du rejet hiérarchique. Le clear ne touche pas aux registres de cible de rendu, le
rebind réécrivait les mêmes valeurs. Les invalidations conservées : `dkCmdBufBindRenderTargets` (macro
`ConditionalZcullInvalidate` de deko3d, à chaque changement d'adresse du depth target), `dk_bind_framebuffer`
(chaque changement de FBO), `dk_rebind_render_target` (uploads, réallocations, resets), début de frame.

Performance (le gain attendu est côté GPU, sur le fill de la passe principale) :
- [ ] `[PERF]` Egypt : `bar_full` encore en baisse d'environ 1 par frame dans les FBO avec profondeur (rebind)
- [ ] FPS Egypt à l'écran : la passe principale (default framebuffer, clear depth puis ~60 draws) doit profiter
      du Zcull ; comparer à Nouveau (69,5 FPS). T-Rex : FBO d'ombre avec depth16
- [ ] Test `fill` GFXBench inchangé ou mieux

Rendu :
- [ ] `--freeze 10000 gl_egypt` et `gl_trex` identiques à Nouveau. Signature d'un Zcull incohérent : fragments
      rejetés à tort (trous, géométrie manquante derrière des surfaces proches) ou surdessin (profondeur ignorée)
- [ ] Spearmint : plusieurs cartes, pas de trous dans les murs ni d'objets visibles à travers

Conformité dEQP-GLES2 (ce sont les lots qui avaient motivé les invalidations Zcull en mars) :
- [ ] `functional.depth_stencil_clear.*`, `functional.fragment_ops.depth*`, `functional.depth_range.*`,
      `functional.clipping.*`
- [ ] `functional.fbo.render.depth*`, `fbo.render.stencil*`, `fbo.render.recreate_depthbuffer*`,
      `fbo.render.recreate_stencilbuffer*`, `fbo.render.resize.*`, `fbo.render.no_rebind*`
      (FBO avec profondeur : le rebind après clear est parti)
- [ ] File d'attente GPU jamais en erreur (`dkQueueIsInErrorState`, message `GPU queue in ERROR STATE` dans
      la sortie nxlink) sur un lot complet enchaînant des centaines de tests sans swap
- [ ] Si une régression apparaît seulement dans les FBO avec profondeur : remettre d'abord le
      `dk_rebind_render_target` après le clear (sans la barrière Zcull) pour isoler les deux sous-changements

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

## Piste B6 — UBO packés poussés seulement quand ils changent

Changement (`dk_shader.c`, `dk_command.c`, `dk_backend.h`, `sgl_gl_types.h`, `sgl_backend.h`, `gl_uniform.c`,
`gl_program.c`, `gl_draw.c`) :
- `dk_bind_program` ne fait plus `dk_alloc_uniform` + `dkCmdBufPushConstants` de chaque UBO packé (VS et FS,
  bindings 0 et 1) à chaque draw. Un bloc n'est poussé que s'il est `dirty` (écrit par `glUniform*`, un miroir
  VS/FS, `gl_DepthRange`, la configuration ou le link) **ou** si sa `gpu_generation` ne correspond plus à celle du
  backend. Sinon il est **relié à son ancienne adresse** (`gpu_offset`), sans aucune écriture : la règle « jamais
  réutiliser une adresse d'UBO dans une frame » (`gfxbench_egypt_far_room_bug.md`, `eaaee0f`) porte sur une
  nouvelle *écriture* à une adresse déjà lue ; relier des données inchangées ne pose pas ce problème. Tout push va
  toujours à une adresse neuve de l'allocateur de la frame.
- `dkCmdBufBindUniformBuffer` reste émis à chaque draw (l'état de binding est perdu aux clears de cmdbuf ; pas de
  suivi de l'état GPU, c'est la piste B7).
- Compteur `uniform_generation` dans le backend (jamais 0), incrémenté à **chaque** point où une adresse
  antérieure peut être réutilisée ou un push enregistré perdu : `dk_reset_uniform_slot` (init, `dk_wait_fence` =
  début de frame, `dk_submit_and_reset` = seuils 4000 draws / allocateurs / `glFinish` / `glFlush` E1, callback
  de dépassement du cmdbuf), chemin ré-entrant du callback (commandes perdues), `dk_ensure_recordable` (cmdbuf
  vidé après swap), `dk_begin_frame` (redondant avec `dk_wait_fence`, gardé par principe), `dk_link_program`
  (relink) et `dk_delete_program` (réutilisation de handle). Les chemins synchrones de texture/FBO
  (`dk_flush_sync` puis `dkCmdBufClear`) ne remettent pas l'allocateur à zéro et soumettent avant de vider : les
  adresses restent valides, pas de bump nécessaire.
- Côté GL : `configure_packed_ubo` et la pré-configuration au link posent `dirty = true`, `gpu_generation = 0`
  (un nouveau programme, `memset` à l'allocation, part aussi à 0) ; la fin de `glLinkProgram` remet `dirty` sur
  tous les blocs valides (relink). Les données initiales Mesa (constantes littérales du constbuf 0) sont
  copiées dans `packed->data` au link, donc poussées au premier bind comme avant.
- `gl_DepthRange` : les 3 floats ne sont écrits (et le bloc marqué dirty) que si la valeur change ; avant, tout
  programme utilisant `gl_DepthRange` était poussé à chaque draw.
- Garde contre le callback de dépassement **pendant** le bind : la génération est lue avant l'allocation et la
  passe sur les blocs est refaite si elle a changé (un bloc poussé avant le reset serait sinon relié à une
  adresse que l'allocateur redémarré peut redonner au bloc suivant du même draw). Ce cas existait déjà avant.
- Non fait, volontairement : les uniforms « legacy » (`sglRegisterUniform`, `vertex_uniforms[]`) sont toujours
  repoussés à chaque draw (même adresse, mêmes données : sans danger, rarement utilisés) ; le `glUniform*` sur
  un programme non courant n'existe pas en GLES 2.0, donc pas de cas « bloc modifié hors binding ».

Performance :
- [ ] Egypt / T-Rex à l'écran et hors écran en A/B (B4 contre B4 + B6) : moins de données inline dans le cmdbuf
      (bones 1,5 Ko par draw skinné, 5 mat4), front-end GPU allégé ; l'offset uniform max par frame doit baisser
- [ ] `driver` (2 500 draws, 9 uniforms par draw, tous changés) : aucun gain attendu, aucune régression
- [ ] Vérifier qu'un programme alterné (A, B, A, B…) sans changement d'uniforms ne repousse plus rien : le cache
      est par programme (`gpu_offset` dans le bloc), pas par slot de binding

Rendu (c'est la vérification critique : adresse d'UBO reliée sans nouvelle écriture) :
- [ ] `--freeze 10000 gl_egypt` identique à Nouveau : plafond, pièce du fond, chevalier (le bug `eaaee0f` se
      manifestait exactement là, à un changement de programme)
- [ ] `gl_trex` : pas de nouvelle dégradation
- [ ] Spearmint : plusieurs cartes, HUD, cinématiques ; aucun objet avec les constantes d'un autre
- [ ] glmark2 (liste du 4 octobre)

Conformité dEQP-GLES2 (VK-GL-CTS, jamais `validation_test`) :
- [ ] `functional.uniform_api.*` (value.initial, value.assigned.by_pointer et by_value, render.*, bool, array,
      struct, sampler : readback `glGetUniform*` inchangé, les données passent toujours par `packed->data`)
- [ ] `functional.shaders.*` (le gros du lot : chaque test dessine plusieurs fois avec des uniforms différents
      → chemin dirty), en particulier `shaders.uniform_*`, `shaders.indexing.*`, `shaders.loops.*`
      (uniforms d'index), `shaders.builtin_variable.*` (gl_DepthRange : dirty seulement sur changement)
- [ ] `functional.depth_range.*`, `functional.clipping.*` (gl_DepthRange + viewport)
- [ ] `functional.flush_finish.*`, `functional.lifetime.*` (relink, suppression/réutilisation de programme :
      `dk_link_program`/`dk_delete_program` bumpent la génération), `functional.state_query.shader.*`
- [ ] `functional.fbo.*`, `functional.texture.*` (chemins synchrones `dk_flush_sync` + clear entre deux draws
      d'un même programme : l'adresse reliée doit rester valide)
- [ ] Régression complète en A/B contre la base d'octobre

## Piste C4 — mémo location → informations pour `glUniform*`

Changement (`gl_uniform.c`, `gl_program.c`, `gl_common.h`, `sgl_gl_types.h`) :
- Chaque `glUniform*` faisait 4 à 5 parcours linéaires des tables de réflexion du programme :
  `find_active_uniform_by_location` (validation du type, puis à nouveau pour le test bool),
  `find_packed_uniform_type`, `lookup_element_stride` (deux fois pour les tableaux) et la boucle des miroirs
  VS/FS de `apply_packed_mirror`. GFXBench `driver` en fait 22 500 par frame.
- Nouveau : table `uniform_cache[128]` par programme, à accès direct par hachage de la location, dont chaque entrée
  mémorise **le résultat de ces mêmes fonctions** pour une location (index dans `active_uniforms`, type packé,
  stride, index du miroir, validité pour `glGetUniform*`). Pas de nouvelle logique de résolution : un défaut de
  cache relance exactement les anciens parcours, une collision coûte ce que coûtait chaque appel avant.
- Construction au link (`sgl_uniform_cache_rebuild` en fin de `glLinkProgram` : toutes les locations de base
  de `program_uniforms` sont pré-résolues ; les éléments de tableau `base + n*stride` le sont au premier
  usage). Invalidation en O(1) par génération (`uniform_cache_gen`, jamais 0) : relink, ajout d'un uniform actif
  par `sgl_track_active_uniform` (chemin `sglRegisterUniform` de `glGetUniformLocation`), ajout d'un miroir
  enregistré. Un programme neuf (`memset` à l'allocation) part à génération 0 : aucune entrée ne peut
  correspondre avant la première résolution.
- `apply_packed_mirror` est scindée : `find_packed_mirror` (choix du miroir, **même règle qu'avant** : premier
  miroir du même étage/binding dont la base est ≤ l'offset écrit, et dont le binding miroir est dans la
  plage) et l'écriture elle-même. Observation non corrigée, pour ne pas changer de comportement sans console :
  cette règle choisit le premier miroir « en dessous », pas forcément celui qui couvre la location. Avec deux
  uniforms VS+FS dans un même bloc, un `glUniform` sur le second passe par le miroir du premier (offset
  relatif). Les tests dEQP `uniform_api.*.both` passent aujourd'hui, donc Mesa dispose vraisemblablement les
  deux étages à l'identique ; à instrumenter si un cas « both » régresse un jour.
- `glGetUniformfv/iv` utilisent le même mémo (validité, type) : réponses identiques.
- Les validations (`sgl_validate_*_uniform`) reçoivent l'entrée active déjà résolue au lieu de la chercher.
- Coût mémoire : 128 × 20 o ≈ 2,5 Ko par programme (≈ +4 % de `sgl_program_t`).

Performance :
- [ ] `driver` GFXBench (9 `glUniform*` par draw, 2 500 draws) : `[PERF]` `uniform` (temps par appel) doit
      baisser nettement ; FPS `driver` en A/B (B6 contre B6 + C4)
- [ ] Egypt / T-Rex : gain faible attendu (peu d'appels par draw), aucune régression

Conformité dEQP-GLES2 (VK-GL-CTS, jamais `validation_test`) — ce sont les tests qui exercent chaque résultat
mémorisé :
- [ ] `functional.uniform_api.info.*`, `uniform_api.value.initial.*`, `uniform_api.value.assigned.*`
      (by_pointer / by_value, render / get_uniform, basic / array / struct / nested_struct / bool / sampler,
      variantes `vertex`, `fragment`, `both` : miroirs), `uniform_api.random.*`
- [ ] Tests négatifs : `functional.negative_api.shader.uniform*` (mauvais type, mauvais nombre de composantes,
      count > 1 sur un non-tableau, location invalide → `GL_INVALID_OPERATION` ; location `-2`, `-3`)
- [ ] `functional.shaders.*` (indexing, loops, struct, conditionals : uniforms de contrôle, tableaux avec
      stride Mesa 4 octets contre std140 16 octets ; `shaders.builtin_variable.*`)
- [ ] `functional.state_query.shader.*` (`glGetUniform*`), `functional.lifetime.*` (relink → rebuild)
- [ ] Régression complète en A/B

## Piste B7 — textures liées par étage et par lot, shaders reliés seulement au changement de programme

Changement :
- `sgl_program_sampler_t.stage_mask` (`sgl_gl_types.h`, rempli dans `gl_program.c` par les deux chemins de
  réflexion, Mesa/`.refl` et transpileur) : étages qui déclarent chaque sampler. Avant, un sampler présent dans un
  seul étage était lié **aux deux** (`stage == -1`), soit deux chargements du constbuf pilote par texture et par
  draw ; un sampler VS seul et un sampler VS+FS étaient indistinguables (`shader_binding == vs_shader_binding`).
- `sgl_prepare_draw` (`gl_draw.c`) collecte les handles par étage et par slot, puis appelle la nouvelle op
  `bind_textures(stage, first, handles[], count)` une fois par plage contiguë de slots (`dkCmdBufBindTextures` :
  3 mots + 1 par handle, un seul macro MME, au lieu de 4 mots et un macro par texture et par étage). Le repli
  noir (texture absente ou incomplète), les paramètres de sampler, les barrières `texture_used_as_rt` /
  `cubemap_needs_barrier` / `sampler_dirty` (B2) restent traités texture par texture, **avant** la plage qui la
  contient (`dk_resolve_texture_binding`, `dk_texture.c`). Sans réflexion (`stage_mask == 0`, shaders
  précompilés sans `.refl`) : liaison aux deux étages comme avant ; le chemin « par unité » est inchangé.
- `dk_bind_program` (`dk_shader.c`) n'enregistre `dkCmdBufBindShaders` que si `dk->bound_program != program`.
  Le macro `BindProgram` de deko3d a déjà un chemin rapide quand l'ID de programme est inchangé, mais les
  ≈ 25 mots et l'exécution du macro étaient payés à chaque draw. Points d'invalidation (`bound_program = 0`) :
  **tous** les `dkCmdBufClear` passent désormais par `dk_cmdbuf_clear()` (`dk_internal.h` ; 25 sites :
  `dk_wait_fence`, `dk_submit_and_reset`, callback de dépassement y compris son chemin réentrant,
  `dk_ensure_recordable`, chemins synchrones de `dk_texture*.c`, `dk_texture_copy.c`, `dk_framebuffer.c`),
  `dk_link_program` (nouvelles copies de shaders) et `dk_delete_program` (handle réutilisable). Rien d'autre
  dans le backend ne touche les programmes liés : les UBO utilisent les constbufs d'index 2+, les textures
  chargent le constbuf pilote, les clears et les blits (moteur 2D) ne lient aucun programme, et deko3d ne
  réinitialise le moteur 3D qu'à la création de la queue (`setup3DEngine`).
- Non fait : pas de cache sur les liaisons de textures elles-mêmes (chaque draw les réenregistre, comme avant).

Performance :
- [ ] Egypt / T-Rex : taille de cmdbuf par draw en baisse (≈ 25 mots de BindShaders + 4 mots par texture) ;
      FPS en A/B
- [ ] `[PERF]` `textures` et `program` (temps CPU par draw) en baisse

Conformité dEQP-GLES2 (VK-GL-CTS, jamais `validation_test`) :
- [ ] `functional.shaders.texture_functions.vertex.*` et `*.vertex_fragment*` (samplers VS seuls et VS+FS :
      liaison VS conservée, bindings par étage), `functional.texture.vertex.*`
- [ ] `functional.texture.*` (unités multiples, cubemap + 2D sur la même unité, tableaux de samplers, mélange
      de types, textures incomplètes → repli noir, `glTexParameter` entre deux draws : `sampler_dirty`)
- [ ] `functional.uniform_api.*sampler*` (remap `glUniform1i` → `tex_unit`)
- [ ] `functional.fbo.*` (rendu vers texture puis échantillonnage : barrière avant la plage), `functional.lifetime.*`
      (relink / suppression de programme : `bound_program` remis à 0), `functional.flush_finish.*` (callback de
      dépassement, `dk_submit_and_reset`)
- [ ] `functional.shaders.*` (changements de programme fréquents : rebind à chaque changement)
- [ ] Régression complète en A/B

Autres :
- [ ] Spearmint : scintillement des textures (descripteurs : `dsb st` + `Descriptors` inchangés ; seule la
      commande de liaison change)
- [ ] GFXBench à l'écran : rendu identique à Nouveau (`--freeze 10000 gl_egypt`, `gl_trex`)

## Piste B8 — attributs de sommets déclarés jusqu'à la plus haute location lue par le programme

Changement :
- `sgl_program_t.num_attrib_slots` (`sgl_gl_types.h`), calculé au link (`sgl_program_update_attrib_slots`,
  `gl_program.c`) par les deux chemins de réflexion : plus haute `linked_location` active + 1, colonnes de
  matrices comprises (`mat4` = 4 locations), borné à [1, 32]. Sans réflexion (shaders précompilés sans `.refl`,
  `glLinkProgram` remet la valeur à `SGL_MAX_ATTRIBS` avant le link) : 32 comme avant.
- `glDrawArrays` / `glDrawElements` (`gl_draw.c`, `sgl_draw_attrib_slots`) passent ce nombre à
  `bind_vertex_attribs` au lieu de 32, et ne préparent (`buffer_offset`) / n'examinent (scan d'indices) que ces
  slots. Contrat (`sgl_backend.h`) : le tableau garde ses 32 entrées, `num_attribs` = slots à déclarer.
- `dk_bind_vertex_attribs` (`dk_draw.c`) : la décision « rien d'activé et tout par défaut → ne rien lier » est
  prise sur les 32 entrées, comme avant, pour que ce comportement ne change pas ; `numAttribs` ≥ 1.
- Ce que fait deko3d (`gpu_3d_vbo.cpp`) : `dkCmdBufBindVtxAttribState` écrit **toujours** les 32 registres
  `VertexAttribState` et marque les slots au-delà de `numAttribs` `IsFixed` (constante, pas de fetch) — c'est
  le bit `isFixed` de `DkVtxAttribState`, que deko3d n'expose pas autrement (pas d'API pour la valeur
  `VTX_ATTR_DEFINE` du constant, d'où le buffer de constantes conservé pour les slots lus). La taille de la
  commande d'attributs ne change donc pas ; les gains sont ailleurs : moins de slots de buffers
  (`BindVtxBufferState` / `BindVtxBuffers`, 4 à 9 mots par slot), plus de fetch pour un tableau activé que le
  shader ne lit pas, et moins de constantes écrites (préparé pour C3).
- Risque examiné : un slot lu par le VS et non déclaré faute GPU (commentaire historique de `dk_draw.c`). Les
  locations viennent de la réflexion du VS **lié** (Mesa élimine les entrées mortes ; transpileur : liste des
  `attribute`), donc tout slot lu est ≤ `num_attrib_slots - 1`.

Performance :
- [ ] Egypt / T-Rex : `[PERF]` `attribs` (temps CPU par draw) en baisse ; FPS en A/B
- [ ] Pas de faute GPU (`dkQueueIsInErrorState`) sur une longue session Egypt / T-Rex / spearmint

Conformité dEQP-GLES2 (VK-GL-CTS, jamais `validation_test`) :
- [ ] `functional.attribute_location.*` (`bind`, `bind_aliasing`, `bind_max_attributes`, `bind_relink`,
      `bind_hole`, matrices : colonnes comptées, locations élevées)
- [ ] `functional.vertex_arrays.*` (single_attribute, multiple_attributes : tableaux activés au-delà des
      attributs lus, strides, GL_FIXED, client arrays, `first` > 0)
- [ ] `functional.draw.*` (draw_arrays / draw_elements, tous types d'indices, EBO et indices client)
- [ ] `functional.state_query.*` (`glGetVertexAttrib*` : valeurs génériques inchangées),
      `functional.shaders.*` dont `shaders.linkage.*` (attributs matriciels, attributs inactifs : `glVertexAttrib4f`
      sur une location non lue), `functional.lifetime.*` (relink : `num_attrib_slots` recalculé)
- [ ] Shaders précompilés avec et sans `.refl` (`examples/01..05`, `validation_test` 226 tests) : chemin 32 slots
- [ ] Régression complète en A/B

Autres :
- [ ] Spearmint : rendu identique (attributs désactivés lus comme la valeur générique courante)
- [ ] GFXBench à l'écran : rendu identique à Nouveau (`--freeze 10000 gl_egypt`, `gl_trex`)

## Piste B5 — non retenue

Les barrières `None + L2Cache | Descriptors | Zcull` après soumission (`dk_begin_frame`, `dk_submit_and_reset`,
callback de dépassement) semblent redondantes avec `Queue::postSubmitFlush` de deko3d, mais le correctif du
scintillement des textures de spearmint (27 février) a montré qu'il fallait **en pratique** `Descriptors` dans ces
barrières, avec `dsb st` après les écritures de descripteurs. Gain d'une barrière par frame : pas de prise de risque
sans console. Gardées telles quelles.
