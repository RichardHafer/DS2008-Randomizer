// Character/resource archives that are safe to retain independently of their
// authored encounters. Paths are passed to DS1 StreamManager::Load without the
// .str suffix, exactly like the game's own character-stream callers.
//
// These archives provide rigs, animations, materials, effects, event BNKs and
// small character-local banks.  Voice/movement/attack samples may live in the
// separate global_enemy_audio_stream_manifest.inc.  A spawnable actor still
// requires a reviewed target SimGroup sidecar; loading either resource class
// alone must never be treated as proof that an actor target exists.
{"global_assets\\global_assets\\ds_assets\\char_str\\npc\\brte_cct\\brute", 0x9C66449Du},
{"global_assets\\global_assets\\ds_assets\\char_str\\npc\\brte_cct\\super_brute", 0xA6BCAFA2u},
{"global_assets\\global_assets\\ds_assets\\char_str\\npc\\corbod_cct\\corruption_body", 0xA0B5A845u},
{"global_assets\\global_assets\\ds_assets\\char_str\\npc\\div_cct\\divider", 0xE50619CFu},
{"global_assets\\global_assets\\ds_assets\\char_str\\npc\\exp_cct\\exploder", 0xBD951E79u},
{"ds1_randomizer_assets\\sup_expl", 0xD5100010u},  // ours: a second exploder look
{"global_assets\\global_assets\\ds_assets\\char_str\\npc\\flier_cct\\flier", 0u},
{"global_assets\\global_assets\\ds_assets\\char_str\\npc\\guarpd_cct\\guardian_pod", 0x7955929Du},
{"global_assets\\global_assets\\ds_assets\\char_str\\npc\\guar_cct\\guardian", 0x0BE1199Au},
{"global_assets\\global_assets\\ds_assets\\char_str\\npc\\Hunt_cct\\hunter", 0xE9CA5A05u},
{"global_assets\\global_assets\\ds_assets\\char_str\\npc\\inf_cct\\infector", 0xA4132B69u},
{"global_assets\\global_assets\\ds_assets\\char_str\\npc\\leap_cct\\leaper", 0x002DFCF9u},
{"global_assets\\global_assets\\ds_assets\\char_str\\npc\\leap_cct\\super_leaper", 0x3C9DCC6Eu},
{"global_assets\\global_assets\\ds_assets\\char_str\\npc\\lurk_cct\\lurker", 0xD39D2999u},
{"global_assets\\global_assets\\ds_assets\\char_str\\npc\\lurk_cct\\super_lurker", 0xB30798D4u},
{"global_assets\\global_assets\\ds_assets\\char_str\\npc\\Pois_cct\\poison_pod_b", 0u},
{"global_assets\\global_assets\\ds_assets\\char_str\\npc\\Pois_cct\\poison_pod", 0u},
{"global_assets\\global_assets\\ds_assets\\char_str\\npc\\preg_cct\\pregnant", 0x1DB8E799u},
{"global_assets\\global_assets\\ds_assets\\char_str\\npc\\swrm_cct\\swarm", 0u},
{"global_assets\\global_assets\\ds_assets\\char_str\\npc\\zomB_cct\\super_slasher", 0x494EAD7Cu},
{"global_assets\\global_assets\\ds_assets\\char_str\\npc\\zomB_cct\\twitcher", 0x422EAC26u},
{"global_assets\\global_assets\\ds_assets\\char_str\\npc\\zomB_cct\\zombie", 0x117742B3u},
{"global_assets\\global_assets\\ds_assets\\char_str\\npc\\zomB_cct\\zombieArmor", 0x3D116F34u},
{"global_assets\\global_assets\\ds_assets\\char_str\\npc\\zomB_cct\\zombievar1", 0x92BD2613u},
{"global_assets\\global_assets\\ds_assets\\char_str\\npc\\zomB_cct\\zombievar2", 0x0A0414EDu},
{"global_assets\\global_assets\\ds_assets\\char_str\\npc\\zomB_cct\\zombieWoman", 0x7CEBDA17u},
