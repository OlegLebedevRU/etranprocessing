# Проверка исторических SHA-256 входов 17E

Статус: `VERIFIED` для точной привязки байтов `LF_CRLF_ONLY` по §10.4 `PROMPT-STANDARD.md`.
Это read-only документальная сверка Git; runtime, тесты и серверы не изменялись.

Для каждого пути ниже SHA-256 из принятого handoff вычисляется из
CRLF-представления указанного raw Git blob. Конверсия выполнена только для
доказательства происхождения исторического digest; новый digest — SHA-256
исходных LF-байтов Git. Для потребления проверять именно Git/raw SHA-256.

Временная копия working tree не является источником digest. Все старые
handoff-блоки остаются неизменными. `producer_commit` каждого блока указан
в заголовке; если отчёт опубликован отдельным коммитом, в таблице указан
его точный artifact commit.

## H-L4D-06C-MB-v1

- `contract_version`: `1.0.0`
- `producer_commit`: `c91b24cc155dc0013500162c6e517897d8074a42`
- CRLF-only артефактов: `6` из `6`; остальные совпали по Git/raw.

| Путь | Historical SHA-256 | Git/raw SHA-256 | Artifact commit |
|---|---|---|---|
| `MenuBuilder/backend/app/routers/settings.py` | `f9f34368b347dc67e9481d7c319d913dfb3ef5fbb99bd41123b5fe4f34245ad4` | `222b65fb19f43a3a5a003ce07a9cae27660694b2fe83049254c87bb77ee883c5` | `c91b24cc155dc0013500162c6e517897d8074a42` |
| `MenuBuilder/backend/app/services/terminal_onboarding_service.py` | `54ba3aff7c3d35a99314d7759cf2714b685deeac8287b564990a33b817606089` | `04b2836d941e5422bdd8de77c2c0f39e13b67114b8fe0aec8632160f0b8e1c58` | `c91b24cc155dc0013500162c6e517897d8074a42` |
| `MenuBuilder/frontend/src/routes/settings/TerminalsSettingsPage.tsx` | `71d4d891c6b573858c1c4a1e1005a8e11847f0d529428178bd1cb509f8706827` | `71eb64bbdfb3d4f47574718952a88916eecbed4f2545911a0947336665d42542` | `c91b24cc155dc0013500162c6e517897d8074a42` |
| `MenuBuilder/frontend/src/api/settings.ts` | `a49a54e4a2b4f98cafbc33f62b15ebc140fc60fddf16c9f4c1a93501af16c093` | `4a862d56605e6051c573616225df23c51bd767a4795982d99b8611ef65e2e04f` | `c91b24cc155dc0013500162c6e517897d8074a42` |
| `MenuBuilder/backend/tests/test_terminal_onboarding.py` | `2d710f66d97adf541ed472cf4ba9914637ffcbb89b7726e818fabcc47262954e` | `f3c256f1756984ab26d3f49d22473bd43c42048e7487982fccf22945cb2e272b` | `c91b24cc155dc0013500162c6e517897d8074a42` |
| `MenuBuilder/docs/l4desk/handoffs/L4D-06C-MB-FIX-01-report.md` | `80c9b55ae6e814a0f3c6709846029f63e32e660dd8f9b544d6cd422bb24429ca` | `a92efe0dd04d2b213b5fa4d375a0b454f9ec300e1a532b75bfcba79b1966b577` | `8004b5b575a72a70e33a7ded11346c8fb4df0e02` |

## H-L4D-08B-MB-v1

- `contract_version`: `1.0.0`
- `producer_commit`: `ad5a13d9fce804746f4f961812b8a026ba416bf4`
- CRLF-only артефактов: `13` из `14`; остальные совпали по Git/raw.

| Путь | Historical SHA-256 | Git/raw SHA-256 | Artifact commit |
|---|---|---|---|
| `MenuBuilder/backend/app/services/remote_session_use_case.py` | `e83af253732b11d89e63c18562b563420ae6b228492f40eee74d27de07df2389` | `8d4f6505759b395cba3b61d1794bb535627028de5fc0e84613acb835f4e3fb7f` | `ad5a13d9fce804746f4f961812b8a026ba416bf4` |
| `MenuBuilder/backend/app/services/media_orchestrator_client.py` | `65b07e3b35eccbfa412ec75a8b1362f1cdf682107af51385e5e06e4d8652ed8a` | `34c1b67275c61f127910eaaefea3a0e651ae78bdef679f84870b42590f6a56dc` | `ad5a13d9fce804746f4f961812b8a026ba416bf4` |
| `MenuBuilder/backend/app/services/remote_session_policy.py` | `688e4d3d31ab4c623d4bc85f602ede31bb935ee39394e88fd3de08c5444e5ca5` | `b53e324b2ec1b08067dfc7e28dc765f0b3e04b65e9028581d094092ff2f4eaae` | `ad5a13d9fce804746f4f961812b8a026ba416bf4` |
| `MenuBuilder/backend/app/routers/remote_sessions.py` | `f6612a7f679e6e1c73d74128911dcbe608778fb9d8212ac2f93096777e94dd72` | `577c7c2f596622e04546d9011bd6bc658dcf718e09761bab958b6761d0b2af63` | `ad5a13d9fce804746f4f961812b8a026ba416bf4` |
| `MenuBuilder/backend/app/routers/video_control.py` | `0ec7212e941b825407a15e47d68fd499298b5f230e6e1e963848f94e09f1d69b` | `abad7f059fc9b132f5974aaeb652bb8cd94d7e51914827d1be07603f89178225` | `ad5a13d9fce804746f4f961812b8a026ba416bf4` |
| `MenuBuilder/backend/app/routers/video.py` | `ac7856605aa42cc24db1ec8657d5f5959e6496a35e5940ff68dbc614f78f03bc` | `bd029bc61d6ffe9dd46b895b9f1925ed96b4532791365df110b666513d0c0831` | `ad5a13d9fce804746f4f961812b8a026ba416bf4` |
| `MenuBuilder/backend/app/repositories/l4desk_repository.py` | `46947572410dab6e163f7ca9f3f22644ef8c33c3629edef51709915a81aefb23` | `9ea21ebaffae4f3409ac984913476ca943cb046fd612b510afe6d5479435e314` | `ad5a13d9fce804746f4f961812b8a026ba416bf4` |
| `MenuBuilder/backend/app/main.py` | `d54d19cfe79fc00a2c1d3db397cb83c44759f02c018742c0131676e716626e55` | `6d46fbde868cd861c2ac838d4ac4919e868bf6cd0c0b6ef924595880b70f950a` | `ad5a13d9fce804746f4f961812b8a026ba416bf4` |
| `MenuBuilder/frontend/src/api/video.ts` | `ae38c22884ad8246cbcaefb70aa49be5b42687dfa477f7b19860b0224c789ea7` | `e751183c40b7cbf89e7b9be0ea20447cdfbc35088ee4b785c76223c562f35e73` | `ad5a13d9fce804746f4f961812b8a026ba416bf4` |
| `MenuBuilder/frontend/src/routes/video-surveillance.tsx` | `87f8dbc8e396bad3d6c35005d1672cc9a48815afbb894323378bf9e095b72d14` | `8c59285efc9b95b9017ef140f5861fbdead3140a2fbbe6b4952001ecb14fad4f` | `ad5a13d9fce804746f4f961812b8a026ba416bf4` |
| `MenuBuilder/frontend/src/routes/devices/DeviceConsoleTab.tsx` | `d8b93b0ab0285781f33efba06dfca6d3469ddf2872ec48ec8b3333e7fc7a1cf5` | `116e499c657d3a1ddf2b6fde42232cb0f81af3c5917e8b89d086783ae16b8966` | `ad5a13d9fce804746f4f961812b8a026ba416bf4` |
| `MenuBuilder/backend/tests/test_remote_session_orchestration.py` | `c34880ca65b4ac4b81a25b26034d1c2317f9f224c46a189cbcd450fcf3c030f1` | `84cf95c88d1cd2fa919e90b637b801e70cca19ac6420b7b614c21cd65b4753a6` | `ad5a13d9fce804746f4f961812b8a026ba416bf4` |
| `MenuBuilder/docs/l4desk/handoffs/L4D-08B-MB-report.md` | `7c442c52c20e5944c667d14869f791f455d7e0de12802e511f79c672a9b206f9` | `4d84af3bb79ad8801cd0dff6b3a6bdeb5fac619a9d6c97fa76b11b78adac3983` | `8bd1bc52cf2614188f82745c0dcc49a8562792b6` |

## H-L4D-09-MB-v1

- `contract_version`: `1.0.0`
- `producer_commit`: `3d0dddba68df211a1d6c89844e0311b43bccd44d`
- CRLF-only артефактов: `13` из `14`; остальные совпали по Git/raw.

| Путь | Historical SHA-256 | Git/raw SHA-256 | Artifact commit |
|---|---|---|---|
| `MenuBuilder/backend/app/main.py` | `ea8e5f4bc9ff71ffd45d8aee6533be20fc2081304d73f6f5dfd66fcb2e0ac74a` | `2927dfb4cd1037f5b7dd80f3c3412d048e1c3dbaa3c43cf4480ef72a9200ee20` | `3d0dddba68df211a1d6c89844e0311b43bccd44d` |
| `MenuBuilder/backend/app/repositories/l4desk_repository.py` | `5cd4aa664214126f3cb8dba146d1da14400081cf551992a4a944e3d24cfabcb7` | `3506fcecb7273db4fae61fd4b06f84b96445cd99ac5cf0defd7fab07d5e6bfe9` | `3d0dddba68df211a1d6c89844e0311b43bccd44d` |
| `MenuBuilder/backend/app/routers/finance.py` | `2b9cfab30ae756600b413fa2b2dd2c111f22c93a712643a1d22569d33ea93974` | `3d0b36bac044a985237bb7941d3d9619d12362bef39c834576fae489ac249a50` | `3d0dddba68df211a1d6c89844e0311b43bccd44d` |
| `MenuBuilder/backend/app/services/financial_core/__init__.py` | `8fb7e50c8d1b8c0da185f3daf785c821d16270ccb47a71442fbc4d01055d582b` | `20b59083b179174cea627dedd5e87bfe08ca099d0a0bcbafe056b6b5728a81b2` | `3d0dddba68df211a1d6c89844e0311b43bccd44d` |
| `MenuBuilder/backend/app/services/financial_core/accounts.py` | `731512c8cc5e10c65325b6f746c259f5a01cc0c15e39eaba39e612c8b060c2a8` | `fcc848270fc96fa9513689291dfcfe97fc81c7c93776a34c50303b3a74f73d69` | `3d0dddba68df211a1d6c89844e0311b43bccd44d` |
| `MenuBuilder/backend/app/services/financial_core/exceptions.py` | `d075e093ca2e8c33503af5a2b16f84946dbcf7b3412f6f6a9d86e31d8fda173d` | `b094de2cf1641c0816c280b6b0bc3ac151b690780bb21b4727604c4f2acf28ed` | `3d0dddba68df211a1d6c89844e0311b43bccd44d` |
| `MenuBuilder/backend/app/services/financial_core/posting.py` | `e0033fcf38e1d0a704a3aabb9c8decd44bfb0533c3e8eac67e27879e3557a30b` | `dc5f65a11870d99726c08cb9acc205aff6bbcbd09fa86d6ea3d7759ab9a9f547` | `3d0dddba68df211a1d6c89844e0311b43bccd44d` |
| `MenuBuilder/backend/app/services/financial_core/projection.py` | `765a69fad521a893d7cd5fe0f7325f190d53645627aa609ae8e0297a71654af0` | `2f8c721ad8ab00bd8fbb31c16409b8c9e9ae84d434420e164e1c7b503b836484` | `3d0dddba68df211a1d6c89844e0311b43bccd44d` |
| `MenuBuilder/backend/app/services/financial_core/reconciliation.py` | `64169deadcc64c71187ed0b3f9131a27c36ee8e8490faddd75bbcde91f83064f` | `2a451bae6decd867bbd83cfde9e809ad7e0d40a9e6648af2c022e9cdc913d098` | `3d0dddba68df211a1d6c89844e0311b43bccd44d` |
| `MenuBuilder/backend/app/services/financial_core/reversal.py` | `a0c39a49c132b88f159e6dcd7c5df1e733941ffe073b1b2701a93dc8bee8c7a1` | `f5111df321deadaa0be6342b4b92adb421d729c9fb77d9bbda4acc19b8ed6ae8` | `3d0dddba68df211a1d6c89844e0311b43bccd44d` |
| `MenuBuilder/backend/app/services/financial_core/schemas.py` | `6f839a54bacc139225a20164a3c22623e4e71e845fd112891a4edfb09c8984f0` | `f15e9c5cffa58c61d70721e2d6547c6549f1f7e58240cc61828cb45fefe3c4e9` | `3d0dddba68df211a1d6c89844e0311b43bccd44d` |
| `MenuBuilder/backend/tests/test_financial_core.py` | `5b01420ff87ed7d42af838344e79c63b2087d41c39cc74ac404984dac4279aea` | `b0afb03d1d98701ecf711da0fb724023a581494fa00b1bcb9360e4a9d95f9b6f` | `3d0dddba68df211a1d6c89844e0311b43bccd44d` |
| `MenuBuilder/docs/l4desk/handoffs/L4D-09-MB-report.md` | `729436d7a0ba76b601acc1261287b6264391fc48a5a44f888fc37d71ba8791b8` | `c4e2b1907f956f639a50ee3aa7c773fe91e7a4f0fb634483b527f8b3c8944516` | `d487bc23011d90cad1ab9945abf480fca73e397a` |

## H-L4D-10-MB-v1

- `contract_version`: `1.0.0`
- `producer_commit`: `2d567c2262e8b37022312427e2f77ba21b61f144`
- CRLF-only артефактов: `13` из `13`; остальные совпали по Git/raw.

| Путь | Historical SHA-256 | Git/raw SHA-256 | Artifact commit |
|---|---|---|---|
| `MenuBuilder/backend/app/repositories/l4desk_repository.py` | `78b98c9bd4d92c64e7e9161084cb2f226a796ee5bd06242c8feea6377d738085` | `04fee4dfd61256c862e1433c2cbec9ad27d5f9839fca0d62cde0b716f9a6ede0` | `2d567c2262e8b37022312427e2f77ba21b61f144` |
| `MenuBuilder/backend/app/routers/finance.py` | `29e5e214ab32e270f3320d4cc22b32a537d3377a8b2e143dd59f543cf61eda4f` | `b5e8cea50a2fd461c661788f4b624a002bf9a18e28aff69701c4c34f0b6f8249` | `2d567c2262e8b37022312427e2f77ba21b61f144` |
| `MenuBuilder/backend/app/services/financial_core/__init__.py` | `73cbaa7f3784d4983331353ca11c394f96de2816736280a339d231f9f821c746` | `e41c2226928d17460715c0a8364f0ae42e630547cc37edcbedd3ec4b0486a782` | `2d567c2262e8b37022312427e2f77ba21b61f144` |
| `MenuBuilder/backend/app/services/financial_core/cycles.py` | `bc72f8e02692feba1d7294185b7511880d1fba324ba599694f0e35424189dcb8` | `4129873bc5e5572eeb90d66f66bf1ea6f7c863df663f9de80b91469c791765d7` | `2d567c2262e8b37022312427e2f77ba21b61f144` |
| `MenuBuilder/backend/app/services/financial_core/exceptions.py` | `19fa02c95d11aa6c1b80eb99150652c73322835d14f6086c12713c862b6d9067` | `f7f9fd00ee723dbc2140011cc2d7ebae7b4cbd6bd58b9ea7a80de1582bd17d73` | `2d567c2262e8b37022312427e2f77ba21b61f144` |
| `MenuBuilder/backend/app/services/financial_core/metering.py` | `f4b05a9f574ea94b121400e1f493e19998d880b4b6306fc98b993af192ca4835` | `ccc638d752f3ed4649329e3d1fdeca9f3f7f52b91e6fcf7f47f9486a90908ff3` | `2d567c2262e8b37022312427e2f77ba21b61f144` |
| `MenuBuilder/backend/app/services/financial_core/schemas.py` | `cf92379b04b71cd37d49d268a63796ad2b0504bb81f139caa9eb91c3f643123d` | `093846c7c79381e7a7c7915b4f60a203530c7598078d672ba221cfd5a11d7236` | `2d567c2262e8b37022312427e2f77ba21b61f144` |
| `MenuBuilder/backend/app/services/financial_core/tariffs.py` | `a09e6c426441e0d9dffa9ccf8389b35c36aaf5f5d1ed480f4c1b8ca87c2f8855` | `bc212be1a6ddf8390ef18ce90cf5d6c21edaa7fd22cc5c83d1aa7eb4f1df6e54` | `2d567c2262e8b37022312427e2f77ba21b61f144` |
| `MenuBuilder/backend/app/services/financial_core/terminals.py` | `cdce9eec1abfcb91aa23b587dba7616a3daf65ce440c7dce9a412218dce49eaf` | `f84f7d165d2cfb24fa6b29898cb8ae723e6e1762cb8aea5877203e421aa6898a` | `2d567c2262e8b37022312427e2f77ba21b61f144` |
| `MenuBuilder/backend/app/services/financial_core/timezones.py` | `d0d8a075cecbc4f4cfe372e0f19340d593fa44f52abec095c48e73b7077e6935` | `161bbe2eae6d95fb331806d06d84e45178f9d72977cd4ad29046d41006c0310a` | `2d567c2262e8b37022312427e2f77ba21b61f144` |
| `MenuBuilder/backend/tests/test_tariffs_and_metering.py` | `19164d31773a7bb624276d5c070f6b8bbc556b3e94e0f71c71428ddf84e639bf` | `aeaa84f33daafcb8c3c51fb5e9d064b1d8d219490fdcddb4c3dddbae37de16cd` | `2d567c2262e8b37022312427e2f77ba21b61f144` |
| `MenuBuilder/backend/tests/test_terminal_onboarding.py` | `0ab63806bcc023406611f669f9424ff2bbad6993a7c0f62d025cc4b63db9fd8c` | `1283dc40ff6f5d1676b4a80c70eb79f0c0fcef23291ddfd8278d778f059a56fb` | `2d567c2262e8b37022312427e2f77ba21b61f144` |
| `MenuBuilder/docs/l4desk/handoffs/L4D-10-MB-report.md` | `8bca1e950bfcc9c873c07d3845e38118800586962e41f4777efcf5439bf627b7` | `e4088736c801a59d5cba092ef0a7182e6810f8ae582dd876aec097dba6315bc8` | `d7fa495eacd8f80f72dfefa0dcc4d23b5a6b1be8` |

## H-L4D-11-MB-v1

- `contract_version`: `1.0.0`
- `producer_commit`: `b6f793ad9880cf20489fe37366edc66af8229464`
- CRLF-only артефактов: `9` из `10`; остальные совпали по Git/raw.

| Путь | Historical SHA-256 | Git/raw SHA-256 | Artifact commit |
|---|---|---|---|
| `MenuBuilder/backend/app/repositories/l4desk_repository.py` | `b8670d8abb462ae2a0a61bf6e782e82482403bf683ddc31345cc3c369e7d5714` | `699d6d4c3b0ddb15d298bbf291e99e75e2789e0b91c3b331bf33585b1250c6eb` | `b6f793ad9880cf20489fe37366edc66af8229464` |
| `MenuBuilder/backend/app/routers/finance.py` | `afeb55ca680d92a754923a575bbc3ee7eeb509322da9ecfebb8b957e7efadb45` | `d78fd08fd0bca002fcbffd44097db4cba9d1b432da6f8a0f8205d9976eccd16f` | `b6f793ad9880cf20489fe37366edc66af8229464` |
| `MenuBuilder/backend/app/services/financial_core/__init__.py` | `6693513b08f168e6cb3a21f4bce6ec06773a59e3970654c2994ef78446907ae7` | `d9a60f8a1deb06735c1b0ce921910fb3b1f955ec2f257198a5b0ccf38f4707a1` | `b6f793ad9880cf20489fe37366edc66af8229464` |
| `MenuBuilder/backend/app/services/financial_core/schemas.py` | `86c06f08d3f556925d3d8a9ae44d1a514a0a27f948e4af28b8a43605aaa53576` | `73b7ba096e1c0f6468a38e1ccac8f675cc689659dd046e1d96eeb7474fd3d88c` | `b6f793ad9880cf20489fe37366edc66af8229464` |
| `MenuBuilder/backend/app/services/financial_core/payments.py` | `6abbe80dbc69d60e0c79abc08391a3bb0f3df13cc8a36a4c86af36394f65c401` | `1f43b11f6be314c3ad2fef4ca6845f6cc566d5807acf36d271ba69ab984a196b` | `b6f793ad9880cf20489fe37366edc66af8229464` |
| `MenuBuilder/backend/app/services/financial_core/manual_payments.py` | `dd68c81271233654ca31a64cf57a7a559718b264225191157475f559042a1950` | `d07ce43567ea5f0ec5c234a652ef02e9bb366cac765f93d08f9b111cb1071de2` | `b6f793ad9880cf20489fe37366edc66af8229464` |
| `MenuBuilder/backend/app/services/financial_core/yookassa.py` | `aac015319eb62683c884c2a52933b26f41b49792ebc72c1272e78823dba10713` | `f23c366e3abb786722a8f3a269653d148eaac86fa610a2358cd6a6876ab3ecdc` | `b6f793ad9880cf20489fe37366edc66af8229464` |
| `MenuBuilder/backend/tests/test_yookassa_and_manual_payments.py` | `655b60e3206ad5ac039f3fe794afb83254327876bf5682eb102b47e20c97098e` | `a2e55f5ef18bd7bc3d24639e3b05eda645ee408ac9315752c54566a061363088` | `b6f793ad9880cf20489fe37366edc66af8229464` |
| `MenuBuilder/docs/l4desk/handoffs/L4D-11-MB-report.md` | `f61b7a366875c07c908161f04e65f6431197cb44f9bd1e5194fe6111be50decf` | `86e23a392e922f7f1735fcf12e7190d78bd940eb2f50e03e954439902132ff9c` | `221f36dbbe36c581c69a3a65deeb1378b90ab123` |

## H-L4D-12-MB-v1

- `contract_version`: `1.0.0`
- `producer_commit`: `95ba8b7915c1d9e34e76169706b5aa862c2e9954`
- CRLF-only артефактов: `13` из `14`; остальные совпали по Git/raw.

| Путь | Historical SHA-256 | Git/raw SHA-256 | Artifact commit |
|---|---|---|---|
| `MenuBuilder/backend/app/main.py` | `a57b8afddfbaac01772f3f05dbae4b7892a864bcf24283eddb3ce1cad1e3f091` | `a6c46a8ab53ad2887b96432bc6d6dd0149e2b7c48e659bd5d2dff580c9b2a1c9` | `95ba8b7915c1d9e34e76169706b5aa862c2e9954` |
| `MenuBuilder/backend/app/repositories/l4desk_repository.py` | `d6b7c1421174016861b75375483463b74eb24336686c9f4da76922d927ec38b0` | `3e02b0e77a6277d5dc6bdab21be0184475a8cab3f764788557ea08b1987dab34` | `95ba8b7915c1d9e34e76169706b5aa862c2e9954` |
| `MenuBuilder/backend/app/routers/finance.py` | `5b0bf751e3149681624ae213cc2ccd4747f55093c76cf1a3ab5c8770731938b1` | `afa8baf47873474a1f1b68414048849ec435bdc5ff05099d1a687a0f665c1351` | `95ba8b7915c1d9e34e76169706b5aa862c2e9954` |
| `MenuBuilder/backend/app/routers/video_control.py` | `08af1e5fcd9f7e24ac4aba869dcc84eccfb16b304ff2026f89495d1785d2e7c1` | `bf3d377567ffac918cafa06a1a9748b14c4f4d484970059e91470ebabe7f9e67` | `95ba8b7915c1d9e34e76169706b5aa862c2e9954` |
| `MenuBuilder/backend/app/services/financial_core/__init__.py` | `1e7d2698992f0ecbaf07698279d880731b53bb10372e8096bb89e8fd831e03a1` | `856cb1b0573afe4f7cc43ce0b8dcf35b0b4c1d66103ca16f0050601adfd4cae2` | `95ba8b7915c1d9e34e76169706b5aa862c2e9954` |
| `MenuBuilder/backend/app/services/financial_core/entitlement.py` | `c8928efcd52530c71975bbe9b009e5841b8026d0222ee9d1af114d2003976f57` | `a940be6525cce57fa3b6fb31b238460ba1c67dc522e80418150a65d9713c1940` | `95ba8b7915c1d9e34e76169706b5aa862c2e9954` |
| `MenuBuilder/backend/app/services/financial_core/notifications.py` | `fd414844d5f26a86607057e947f8a87aa54904deee81f6262f5b3266f8e6759b` | `6ba83d55aded4ea63029b70ac713debd90460728278905b12f82d736493a017d` | `95ba8b7915c1d9e34e76169706b5aa862c2e9954` |
| `MenuBuilder/backend/app/services/financial_core/schemas.py` | `8438eda2f13c5cc9729a3398e15390a2453f2cdc55e992f1b3b20db12401b468` | `22befb52881c01bbdc184d07b2d67babcd78500a6c1d041e59126ec561ba4470` | `95ba8b7915c1d9e34e76169706b5aa862c2e9954` |
| `MenuBuilder/backend/app/services/financial_core/stop_outbox.py` | `45119132562a10f1eac9e017c54600c751ff015e67806e1c23b73a6989c127d4` | `5d8e929e10bd41861bb8d372866b79c80c3af6320e40f0943f17bc6e46d4bcfe` | `95ba8b7915c1d9e34e76169706b5aa862c2e9954` |
| `MenuBuilder/backend/app/services/financial_core/worker.py` | `d4e485c04a30af8471fb095476719dcd15169de91945ed232a696acb186e13d3` | `f97f13c824d345776f30ccf838a938950cfbce51454eef5ee2ecc7a6c099c409` | `95ba8b7915c1d9e34e76169706b5aa862c2e9954` |
| `MenuBuilder/backend/app/services/remote_session_policy.py` | `1778f36290937ef239415a55d8581ca812c3e6517b7e6186a90ab6100fa9574e` | `ca123d2682f3b64483267c9374fe94c67af3a3773609a5cf8f78ea4cb12daa53` | `95ba8b7915c1d9e34e76169706b5aa862c2e9954` |
| `MenuBuilder/backend/tests/test_l4d_12_entitlement_grace_and_notifications.py` | `560ae3a021a1ac0943d02f0acfac8091cd14adae75ad175dac0f4ba9d54fbfbd` | `0f944a88945eea116ca310d8174ad2492a17937e93da8a99a5873476c1158ffb` | `95ba8b7915c1d9e34e76169706b5aa862c2e9954` |
| `MenuBuilder/docs/l4desk/handoffs/L4D-12-MB-report.md` | `a04349eff17353edabd0b24dc8612fcd996caeb17e78c87488fd15158aee0873` | `98da22164994ff300ad66bd49024c8f0794d4291df1cfc1053ca43206c9c0979` | `385155dc394f2bd4479f1545695d61a03b485b9b` |

## H-L4D-13-MB-v1

- `contract_version`: `1.0.0`
- `producer_commit`: `f5017615a8a84eee318a78b54a992ceb45f91edc`
- CRLF-only артефактов: `21` из `23`; остальные совпали по Git/raw.

| Путь | Historical SHA-256 | Git/raw SHA-256 | Artifact commit |
|---|---|---|---|
| `MenuBuilder/backend/app/main.py` | `296e0ef944a9e1abcda254a28f19a77b4741e97b6c82b8a01c1379b1313cad06` | `c576cdda5eec41dec837c0f19400133f3fe515193982e4bf9119b8b4c4645219` | `f5017615a8a84eee318a78b54a992ceb45f91edc` |
| `MenuBuilder/backend/app/routers/mcp_waitlist.py` | `6f2bfdf626572eb4cfccbe12c7424bb426aefea365f447ded5e0876174a43095` | `674d1692d2ddfb5503f3c9ece6b52eff2878344b86ebd82ab6069f11f600fab9` | `f5017615a8a84eee318a78b54a992ceb45f91edc` |
| `MenuBuilder/backend/tests/test_mcp_waitlist_and_role5.py` | `e486e24d5d95eec1cc2c1037a4aae342b877216c96d0b6f0385749420a9f96fb` | `b3867e52ac2646bf92bf7d3656cfd440ed50d6d92ba9561f8756d384c09b2056` | `f5017615a8a84eee318a78b54a992ceb45f91edc` |
| `MenuBuilder/frontend/src/App.tsx` | `04856fceb7af3951c448516fb78aad912dd155ca7247fb6d99632139ada9575d` | `7efc495dcbb4291b6114531a6d6ffe2c516c64e82808303150c07ac9bf36ac15` | `f5017615a8a84eee318a78b54a992ceb45f91edc` |
| `MenuBuilder/frontend/src/api/finance.ts` | `aaafeecbe1d0157ce0e06b2aa211a0ae87676592ef625f1f3b8c7cddde296c54` | `54e58a9847574a7990953e77a74ae183af2eb5cb42fb9c7e283e5a4735ec2ffb` | `f5017615a8a84eee318a78b54a992ceb45f91edc` |
| `MenuBuilder/frontend/src/api/mcpWaitlist.ts` | `d1201e858e13306e8d93d70611ce1b9fbaaad9fa0b01793bee988a6ae46e872b` | `3882fc3500be7a854b63ba48fb04a008dc2f2c9e1eac41066b31f8b41de93b69` | `f5017615a8a84eee318a78b54a992ceb45f91edc` |
| `MenuBuilder/frontend/src/components/OnboardingWizardModal.tsx` | `d6497e7df398b0c755071099fa86a45e29fdebe594d61db0e2259f62416b6275` | `03dd2e0ebb84f949281b3bf0a1d20280c33b7b65958ef09a9c53f05068d9ab51` | `f5017615a8a84eee318a78b54a992ceb45f91edc` |
| `MenuBuilder/frontend/src/components/RefusalReasonCard.tsx` | `d63dee75b4eea4e9c5ef1311a3ec767431d1010edfc410ab9858132a1cfee12c` | `7d77f7c0f812c0650da7c9d7825e8ac670ae80cdc4eb4e9e4c3ef0c9667115c3` | `f5017615a8a84eee318a78b54a992ceb45f91edc` |
| `MenuBuilder/frontend/src/routes/console/ConsolePage.tsx` | `4167364fa0454f01aaf6fb5a4059a885be757fe56a4e59e392d6eb9f19606516` | `e7fe93d471c8619f6d9ed1afb0cca020413457acb0f5405558899e266e619209` | `f5017615a8a84eee318a78b54a992ceb45f91edc` |
| `MenuBuilder/frontend/src/routes/devices/DeviceConsoleTab.tsx` | `f7ae07658d3024100708242bb399ea87aaa97c63cffa5f2e9f274d677694a797` | `374f58ecc3c92e41bebc84a301e0f8f64f1be849bdad96ea5c78b19070a9f1fb` | `f5017615a8a84eee318a78b54a992ceb45f91edc` |
| `MenuBuilder/frontend/src/routes/layout.tsx` | `a858ca421328747205c73a759cfb13088f317523f46d5524ac994b89b3a065ee` | `9e7a355219694a0fc5a4ed0828c4eccd4c80341f4aed78a5e7f7a5cb4510db60` | `f5017615a8a84eee318a78b54a992ceb45f91edc` |
| `MenuBuilder/frontend/src/routes/licenses/LicensesPage.tsx` | `13da06cba68c032efd5372f6881668487d6feeb0eec3369139589b6724725541` | `da8ad2b9cd0eda5a3904434c1dcb1713b61e34857f27802f6252fc4df7b56451` | `f5017615a8a84eee318a78b54a992ceb45f91edc` |
| `MenuBuilder/frontend/src/routes/mcp/McpPromoPage.tsx` | `928e63d6b713c9db4c98f615a51a7239d11db96fc581afa8d9fb8ff6ce7ecca5` | `2013a4f0130b18b0a63770ec0141d23c4016d4228431f32e19d7ea3332e0858b` | `f5017615a8a84eee318a78b54a992ceb45f91edc` |
| `MenuBuilder/frontend/src/routes/settings/TerminalsSettingsPage.tsx` | `4d686b27bf504011da0df285383ff45114e19ebd0523a1526f77af65b8cbcb67` | `7e2fecccebeb1bbb4e2b6f8782203c22fa5663275f79b94202d1c0cd006a051d` | `f5017615a8a84eee318a78b54a992ceb45f91edc` |
| `MenuBuilder/frontend/src/routes/video-surveillance.tsx` | `376b6a5884185a2ab6d912b9a8be8c4fee0db9d3789ef8b2163c4e12e7b9475d` | `a9a8bb79338655ae8e35aa611681affd46a92d61d38a7ebed8056b0079ee884e` | `f5017615a8a84eee318a78b54a992ceb45f91edc` |
| `MenuBuilder/frontend/src/tests/l4desk-accessibility-responsive.test.ts` | `2515d12f032e3e4c31e92b69232a4a62b21a474335d1c00b8f85e39a90f8debb` | `0c6311648611fdc789dedad76c75c21c6d8cbdda2bca6b4179c289c5f5f29189` | `f5017615a8a84eee318a78b54a992ceb45f91edc` |
| `MenuBuilder/frontend/src/tests/l4desk-licenses-mcp.test.ts` | `4232e6d90806b8413f465de1682c0422120e0e3f8440c33a1aa887827c21d3fc` | `a9b91da297e760779296d66279c8b1e8e3cead7062e7adf031671559fbfab111` | `f5017615a8a84eee318a78b54a992ceb45f91edc` |
| `MenuBuilder/frontend/src/tests/l4desk-profile-navigation.test.ts` | `80b11223a85e23fe1456914a8280b098a48a495a1a7ec4b7067b1ca71353035e` | `5879a948b8b769e7f973987b74ac8e59d1a3e0e3bf8bcb1c733d1ccb1001a354` | `f5017615a8a84eee318a78b54a992ceb45f91edc` |
| `MenuBuilder/frontend/src/tests/l4desk-refusal-reasons.test.ts` | `95bc280c549c10e7354932bcfff576c660f0a686de26fe9b4cb098a542efa506` | `1cc04af838869e16a3a1622e177226d1213251bdfa72e2d07fb1c02bee1015ae` | `f5017615a8a84eee318a78b54a992ceb45f91edc` |
| `MenuBuilder/frontend/src/utils/navigationProfile.ts` | `bbbf70d9eed9d8e2c2e84960a2195f301a1ba7e1ec0d76662ae8452aee4f2f3d` | `52f0d8f89ed6ee769c15426629256b05ffc15cd4bcc6326d6a9a040d2f7c6e8f` | `f5017615a8a84eee318a78b54a992ceb45f91edc` |
| `MenuBuilder/docs/l4desk/handoffs/L4D-13-MB-report.md` | `b9027374de01a8e01b2c56e7cc30467747fbc35a8aae646ac6a6eadbe7886c1b` | `8fefb77917bb585c77f464ac16134b65b0ece1d2510ead80f0c1a81e2f345a34` | `f5017615a8a84eee318a78b54a992ceb45f91edc` |

## H-L4D-14-MB-v1

- `contract_version`: `1.0.0`
- `producer_commit`: `877dc00ac6e5131c65375c5494659d36ff31822e`
- CRLF-only артефактов: `12` из `12`; остальные совпали по Git/raw.

| Путь | Historical SHA-256 | Git/raw SHA-256 | Artifact commit |
|---|---|---|---|
| `MenuBuilder/backend/app/main.py` | `a372b7571162fb7b366b7e52f15304ef7b9f8f4a0b0445d83f3f96360717125b` | `38dd4077e423bbfe3460abd91032ea844de69707cd2b7532a235bd4529b928d8` | `877dc00ac6e5131c65375c5494659d36ff31822e` |
| `MenuBuilder/backend/app/routers/hub.py` | `1b83e283247f80841bc663e6f982e602d5171d5f9648ee46d53bce2525cbdc49` | `18904ebb3914ee46f894e870fde6dacf903731645157b09b6eeb332a1cf869c0` | `877dc00ac6e5131c65375c5494659d36ff31822e` |
| `MenuBuilder/backend/app/services/financial_core/hub_schemas.py` | `0e4124362c89f0cf6e1d515c0b6f892b02735721e2b9fa883920650911485e2c` | `376f56b208806b052edfe0f5c0a9b60e6cda66dd49678757eb40f01cd23c572a` | `877dc00ac6e5131c65375c5494659d36ff31822e` |
| `MenuBuilder/backend/app/services/financial_core/hub_service.py` | `3d3526ea19b098e0092fff6b1f874fdd666c3c7df64ba6d991c0aa8e8a5aa204` | `595a677506298c0a0cc265e3a172bf55d4df28c3c1c5e768306ad84ea226e8bd` | `877dc00ac6e5131c65375c5494659d36ff31822e` |
| `MenuBuilder/backend/app/services/financial_core/reconciliation.py` | `e2ffd67cd9d985e6be7c218c500b1e39c3cbbe1498769231f9733b866e79f364` | `2bb8243802682619f94560a461455f62c46193f332fbc9d3a3d729fdc62b83ee` | `877dc00ac6e5131c65375c5494659d36ff31822e` |
| `MenuBuilder/backend/tests/test_hub_and_reconciliation.py` | `49509bf568d7c390c122892584b3cdec2cb6a426140d8e74dd0fbf85f9c20100` | `e99e255e92650c31ed1a221e04c32e5625812dbbfe152ac902a6af1e7e6f493c` | `877dc00ac6e5131c65375c5494659d36ff31822e` |
| `MenuBuilder/frontend/src/App.tsx` | `14903c710fd7d6d52f47b16ab3f93cbb2a2f1218a70a22bd6d05f9e8ec9b16a6` | `48a079d74f79a4111fe4b61b91f1b43d4821ec250dcec22670a981629dfe9862` | `877dc00ac6e5131c65375c5494659d36ff31822e` |
| `MenuBuilder/frontend/src/api/hub.ts` | `140d3b74c37b34611f8e68d490e5c47bf263128b1e20f0fdfae74d1b2594dd20` | `bb7be834f2986f3a10746064f89da805237106a7db4be20368fd738c0d94863e` | `877dc00ac6e5131c65375c5494659d36ff31822e` |
| `MenuBuilder/frontend/src/pages/AdminHubPage.tsx` | `a751bc1619d5631ba32be823e2d17f1fe7470597ef11865f66edcf284b06760b` | `b966fa0835a193d4ef659a7a711759df28bfcc5bb921d4a305d98b82a77f3ff9` | `877dc00ac6e5131c65375c5494659d36ff31822e` |
| `MenuBuilder/frontend/src/routes/admin-layout.tsx` | `f73171adb60f790b556951bb955d40e9a865c51a1d4119753d1fefee8fbc4fab` | `142855c01aa7a2cfa59ac518bbbe0233c0e978e15e427aac32e5f0cf27392a7e` | `877dc00ac6e5131c65375c5494659d36ff31822e` |
| `MenuBuilder/frontend/src/tests/l4desk-hub-and-reconciliation.test.ts` | `50deda9a76ee33d48eed55c33b4c5e8f5178d4c5c41fd781f7acd63a7cc08a3c` | `90ae3de80928597b86d2b4300551dfed37f770e6eab0ddfccc5b4b6c64331af4` | `877dc00ac6e5131c65375c5494659d36ff31822e` |
| `MenuBuilder/docs/l4desk/handoffs/L4D-14-MB-report.md` | `43a419248253c609a11c0e5eaeedcef506924720f3969148054e5c1896608098` | `589567ae665d552a3b2e48ed1c31423cc7ff70da98ae38d7875727d474d59baa` | `877dc00ac6e5131c65375c5494659d36ff31822e` |

## H-L4D-16-MB-v1

- `contract_version`: `1.0.0`
- `producer_commit`: `f188da00db13de054497ad8a6e2332b2f6127cc7`
- CRLF-only артефактов: `12` из `13`; остальные совпали по Git/raw.

| Путь | Historical SHA-256 | Git/raw SHA-256 | Artifact commit |
|---|---|---|---|
| `MenuBuilder/backend/app/routers/archive.py` | `a3b51253f51e9c3f913a846d3f3c6fa73ce35270c1d5f6b22ccf65c96497298c` | `5f84fa936c18a55c0e224af81466c252702c298526836e2c9233453d528e573d` | `f188da00db13de054497ad8a6e2332b2f6127cc7` |
| `MenuBuilder/backend/app/services/financial_core/archive_schemas.py` | `506c5d832c1cc68ebca4963e142ddbe7ebe8288cd48e3efc3baa66f9903a7c0a` | `fb900ed7150cf2d638995f4b6b9f3fdfeb4a6cfd72c86ea7f89263621a4db61a` | `f188da00db13de054497ad8a6e2332b2f6127cc7` |
| `MenuBuilder/backend/app/services/financial_core/archive_service.py` | `c4563f3187b494b08b352a0a5d535dd5fa00bae91c28de2f8dde41ee0b555803` | `34ff915831cb077f82581c52878a96af5dc8205f8581e1020efa6ff72c2705d9` | `f188da00db13de054497ad8a6e2332b2f6127cc7` |
| `MenuBuilder/backend/tests/test_archive_manifests.py` | `922187c0d7f2677524eedf138f67c833d27fdcc03821f44f8e14b80a9d6d012f` | `131699bc95400cdd8f400a3107e3696b76f6b3df2a526f9e3f1b9c3579f1fb2a` | `f188da00db13de054497ad8a6e2332b2f6127cc7` |
| `MenuBuilder/frontend/src/tests/l4desk-archive-manifests.test.ts` | `f2372fcc3be3686633c61ed7a0bc362611b6618e8f79e3d3cbe924838e533f1d` | `0a127d35d9cc758f17f241be9830fede4decc6c52b908a071f16bb367676bce2` | `f188da00db13de054497ad8a6e2332b2f6127cc7` |
| `MenuBuilder/backend/app/main.py` | `be450f52d634e73846bc31d8c989e221006e45d051c624919c855037a9d83df4` | `eb80285d62152cb1724dc3855247f5cb1de0e6bd88506f7c3d0bcd5ac05079af` | `f188da00db13de054497ad8a6e2332b2f6127cc7` |
| `MenuBuilder/backend/app/routers/hub.py` | `448bb162cd302f7180daee39f49f09ee7ea11b7cf35f8c497cd67d29a980643f` | `af59d9b66d9c6b49dce87a57d165f6d791d8fd8e9d55c06931112341f3ae4b8a` | `f188da00db13de054497ad8a6e2332b2f6127cc7` |
| `MenuBuilder/backend/app/services/financial_core/__init__.py` | `3f5a16f348887152ae4813ae74e40d165a7313e27b9ec5c8715414fdcf626d2b` | `7d05e35a008380655e5b2f15530fafe436e6e4d16aa9ae167c87e187e9335bda` | `f188da00db13de054497ad8a6e2332b2f6127cc7` |
| `MenuBuilder/backend/app/services/financial_core/exceptions.py` | `abd0efd9e04d714d5af22f8452579995f33ce34e3e4188aa6ff5310ec30fe7b6` | `0f3fc1ca09914d8bfc218c0f83b86e9cb1cda1229fd070413a81acaee431d485` | `f188da00db13de054497ad8a6e2332b2f6127cc7` |
| `MenuBuilder/backend/app/services/financial_core/hub_service.py` | `6c3aaa50dc8d5ca4d91f403438c28d417b90efd6bede99bc883b7e9ae69f96ba` | `fc1ff18fe6646f1a126bb432352ce247d1bd9ad3615a97fb3198784597309a2b` | `f188da00db13de054497ad8a6e2332b2f6127cc7` |
| `MenuBuilder/frontend/src/api/hub.ts` | `456fd6c96ee1c714344e619841811d9a710d0e38894f6f0e28fce41190c294f4` | `3e409230ff87253f2fcc602e9a34adceaa1c8ff01db66123374193d341b28a6f` | `f188da00db13de054497ad8a6e2332b2f6127cc7` |
| `MenuBuilder/frontend/src/pages/AdminHubPage.tsx` | `7b56208aae2bf0edc44853576f85f1e3ba566c2053999821e2bacac853e14227` | `2a5ec2fdcd0653afe5654573771bd4f3eda8ba5a531b3caa2a19129fff3dfb64` | `f188da00db13de054497ad8a6e2332b2f6127cc7` |

## Итог

Проверено 112 пар исторического CRLF и опубликованного Git/raw SHA-256.
Каждое соответствие установлено побайтным LF → CRLF; других расхождений
для перечисленных файлов нет. Изменений содержимого, API и версий нет.

Отчёт `H-L4D-06C-MB-v1` входит в принятый список артефактов, но появился
позже указанного в handoff `producer_commit`; его отдельный artifact commit
`8004b5b575a72a70e33a7ded11346c8fb4df0e02` указан в строке.
Контроллер должен сохранить эту точную привязку в соответствующем блоке
`ARTIFACT_BYTE_BINDING`; отсутствие доступного blob на указанном commit
остановит contract gate.
