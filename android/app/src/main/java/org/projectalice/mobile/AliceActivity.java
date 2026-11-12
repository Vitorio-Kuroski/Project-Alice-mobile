package org.projectalice.mobile;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import android.app.AlertDialog;
import android.app.NativeActivity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.graphics.Typeface;
import android.view.Gravity;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.PopupWindow;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

/**
 * NativeActivity com o minimo que o C++ nao consegue fazer sozinho:
 * pedir acesso aos arquivos, abrir o seletor de pastas do Android e mostrar
 * mensagens. Todo o resto (janela, EGL, toque, jogo) continua na libAlice.so.
 *
 * Os metodos publicos sao chamados pelo C++ (entry_point_android.cpp), de
 * outra thread -- por isso tudo que mexe com a interface vai por runOnUiThread.
 */
public class AliceActivity extends NativeActivity {
	private static final int REQUEST_PICK_FOLDER = 1;

	// a NativeActivity carrega a .so por conta propria (dlopen), o que nao
	// registra os metodos "native" desta classe no JNI; carregar tambem pelo
	// System.loadLibrary resolve (e a mesma .so, nao carrega duas vezes)
	static {
		System.loadLibrary("Alice");
	}

	private static native void nativeOnGameFolderPicked(String path);
	private static native void nativeSetSetting(String key, int value);
	private static native void nativeOnTextResult(String text);
	private static native void nativeContinueGame();

	private boolean waitingForAllFilesAccess = false;
	private AlertDialog folderDialog = null;

	private final Handler uiHandler = new Handler(Looper.getMainLooper());
	private AlertDialog loadingDialog = null;
	private PopupWindow statsPopup = null;
	private SharedPreferences prefs = null;
	private boolean perfCounterOn = false;
	private TextView statsText = null;
	private Runnable pendingLoadingDialog = null;
	private TextView loadingText = null;
	private String loadingMessage = null;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		hideSystemBars();
		prefs = getSharedPreferences("alice_android", MODE_PRIVATE);
		nativeSetSetting("render_scale", renderScale());
		nativeSetSetting("map_scale", mapScale());
		nativeSetSetting("fps_limit", fpsLimit());
	}

	// --- menu "Ajustes do Android" (toque com tres dedos) ----------------------

	private static final int[] RENDER_SCALES = { 50, 60, 75, 100 };

	// Resolucao da tela (interface) e do mapa, que e desenhado nessa fracao da
	// resolucao da tela e ampliado: a interface fica nitida mesmo com o mapa
	// leve. Antes da v0.8 havia so a da tela, com padrao 75%; a chave mudou
	// para os padroes novos valerem para todo mundo.
	private int renderScale() {
		return prefs.getInt("screen_scale", 100);
	}

	private int mapScale() {
		return prefs.getInt("map_scale", 75);
	}

	private int fpsLimit() {
		return prefs.getInt("fps_limit", 60);
	}

	private boolean economyOn() {
		return prefs.getBoolean("economy", false);
	}

	/**
	 * Modo economico: 30 FPS, mapa em 50% (interface em 100%, nitida) e
	 * graficos leves (mapa plano, menos particulas de comercio). Desligar volta
	 * aos padroes: 60 FPS, mapa em 75% e o globo. Cada ajuste continua mudavel
	 * depois, aqui ou nas opcoes do jogo.
	 */
	private void setEconomy(boolean on) {
		int fps = on ? 30 : 60;
		int map = on ? 50 : 75;
		prefs.edit().putBoolean("economy", on).putInt("fps_limit", fps).putInt("screen_scale", 100).putInt("map_scale", map).apply();
		nativeSetSetting("fps_limit", fps);
		nativeSetSetting("render_scale", 100);
		nativeSetSetting("map_scale", map);
		nativeSetSetting("light_graphics", on ? 1 : 0);
	}

	private boolean hapticOn() {
		return prefs.getBoolean("haptic", true);
	}

	/** Chamado pelo C++ quando o toque longo e reconhecido. */
	public void haptic(final String unused) {
		if(!hapticOn())
			return;
		Vibrator vibrator = getSystemService(Vibrator.class);
		if(vibrator != null && vibrator.hasVibrator())
			vibrator.vibrate(VibrationEffect.createPredefined(VibrationEffect.EFFECT_CLICK));
	}

	/** Chamado pelo C++ no toque com tres dedos. */
	public void openSettings(final String unused) {
		runOnUiThread(this::showSettingsDialog);
	}

	private void showSettingsDialog() {
		if(isFinishing())
			return;
		String[] items = {
			"Modo econômico: " + (economyOn() ? "ligado" : "desligado"),
			"Contador de desempenho: " + (perfCounterOn ? "ligado" : "desligado"),
			"Resolução da interface: " + renderScale() + "%",
			"Resolução do mapa: " + mapScale() + "%",
			"Limite de quadros: " + fpsLimit() + " FPS",
			"Vibrar no toque longo: " + (hapticOn() ? "sim" : "não"),
			"Mods: " + (chosenMods().isEmpty() ? "nenhum (jogo base)" : chosenMods().size() + " escolhido(s)")
		};
		AlertDialog dialog = new AlertDialog.Builder(this)
			.setTitle("Ajustes do Android")
			.setItems(items, (d, which) -> {
				switch(which) {
				case 0:
					setEconomy(!economyOn());
					break;
				case 1:
					perfCounterOn = !perfCounterOn;
					nativeSetSetting("perf_counter", perfCounterOn ? 1 : 0);
					break;
				case 2:
					showScaleDialog("Resolução da interface", "screen_scale", "render_scale", renderScale(), 100);
					return;
				case 3:
					showScaleDialog("Resolução do mapa", "map_scale", "map_scale", mapScale(), 75);
					return;
				case 4:
					int fps = fpsLimit() == 60 ? 30 : 60;
					prefs.edit().putInt("fps_limit", fps).apply();
					nativeSetSetting("fps_limit", fps);
					break;
				case 5:
					prefs.edit().putBoolean("haptic", !hapticOn()).apply();
					break;
				case 6:
					showModsDialog();
					return;
				}
				showSettingsDialog(); // mostra o valor novo
			})
			.setNegativeButton("Fechar", null)
			.create();
		showWithoutSystemBars(dialog);
	}

	// --- mods ---------------------------------------------------------------
	// Os .mod ficam na pasta mod/ do Victoria 2. Os nomes escolhidos vao para
	// android_mods.txt (lido pelo C++ ao abrir o app). Latin-1 dos dois lados:
	// o C++ compara os bytes do nome como estao no .mod.

	private static final Pattern MOD_NAME = Pattern.compile("(?m)^\\s*name\\s*=\\s*\"?([^\"\\r\\n]*)\"?");

	private File modsConfigFile() {
		return new File(getExternalFilesDir(null), "android_mods.txt");
	}

	private List<String> chosenMods() {
		List<String> out = new ArrayList<>();
		try {
			for(String line : Files.readAllLines(modsConfigFile().toPath(), StandardCharsets.ISO_8859_1)) {
				if(!line.trim().isEmpty())
					out.add(line.trim());
			}
		} catch(Exception e) {
			// sem arquivo: nenhum mod
		}
		return out;
	}

	private List<String> availableMods() {
		List<String> names = new ArrayList<>();
		try {
			File folderFile = new File(getExternalFilesDir(null), "game_folder.txt");
			String folder = new String(Files.readAllBytes(folderFile.toPath()), StandardCharsets.UTF_8).trim();
			File[] files = new File(folder, "mod").listFiles((dir, name) -> name.toLowerCase().endsWith(".mod"));
			if(files == null)
				return names;
			for(File f : files) {
				String text = new String(Files.readAllBytes(f.toPath()), StandardCharsets.ISO_8859_1);
				Matcher m = MOD_NAME.matcher(text);
				if(m.find() && !m.group(1).trim().isEmpty() && !names.contains(m.group(1).trim()))
					names.add(m.group(1).trim());
			}
		} catch(Exception e) {
			// sem pasta do jogo ainda
		}
		names.sort(String.CASE_INSENSITIVE_ORDER);
		return names;
	}

	private void showModsDialog() {
		List<String> mods = availableMods();
		if(mods.isEmpty()) {
			AlertDialog dialog = new AlertDialog.Builder(this)
				.setTitle("Mods")
				.setMessage("Nenhum mod encontrado na pasta mod do Victoria 2. Copie os mods (o arquivo .mod e a pasta dele) para lá.")
				.setPositiveButton("OK", (d, w) -> showSettingsDialog())
				.create();
			showWithoutSystemBars(dialog);
			return;
		}
		List<String> chosen = chosenMods();
		String[] labels = mods.toArray(new String[0]);
		boolean[] checked = new boolean[labels.length];
		for(int i = 0; i < labels.length; ++i)
			checked[i] = chosen.contains(labels[i]);
		AlertDialog dialog = new AlertDialog.Builder(this)
			.setTitle("Mods")
			.setMultiChoiceItems(labels, checked, (d, which, isChecked) -> checked[which] = isChecked)
			.setPositiveButton("Salvar", (d, w) -> {
				StringBuilder sb = new StringBuilder();
				for(int i = 0; i < labels.length; ++i) {
					if(checked[i])
						sb.append(labels[i]).append('\n');
				}
				try {
					Files.write(modsConfigFile().toPath(), sb.toString().getBytes(StandardCharsets.ISO_8859_1));
				} catch(Exception e) {
					Toast.makeText(this, "Não foi possível salvar a escolha de mods.", Toast.LENGTH_LONG).show();
					return;
				}
				AlertDialog info = new AlertDialog.Builder(this)
					.setTitle("Mods")
					.setMessage("Os mods valem a partir da próxima vez que o app abrir. Feche o app e abra de novo. Na primeira vez com essa combinação de mods o cenário é montado de novo, o que leva alguns minutos. Mods dos quais um escolhido depende entram junto.")
					.setPositiveButton("OK", null)
					.create();
				showWithoutSystemBars(info);
			})
			.setNegativeButton("Voltar", (d, w) -> showSettingsDialog())
			.create();
		showWithoutSystemBars(dialog);
	}

	private void showScaleDialog(String title, String prefKey, String nativeKey, int current, int standard) {
		String[] labels = new String[RENDER_SCALES.length];
		int checked = 0;
		for(int i = 0; i < RENDER_SCALES.length; ++i) {
			int v = RENDER_SCALES[i];
			labels[i] = v + "%" + (v == standard ? " (padrão)" : v == 50 ? " (mais rápido)" : v == 100 ? " (mais nítido)" : "");
			if(v == current)
				checked = i;
		}
		AlertDialog dialog = new AlertDialog.Builder(this)
			.setTitle(title)
			.setSingleChoiceItems(labels, checked, (d, which) -> {
				prefs.edit().putInt(prefKey, RENDER_SCALES[which]).apply();
				nativeSetSetting(nativeKey, RENDER_SCALES[which]);
				d.dismiss();
				showSettingsDialog();
			})
			.setNegativeButton("Voltar", (d, which) -> showSettingsDialog())
			.create();
		showWithoutSystemBars(dialog);
	}

	// O sistema mostra as barras de novo em varias situacoes (dialogos,
	// teclado, voltar de outro app); escondidas de novo ao recuperar o foco.
	@Override
	public void onWindowFocusChanged(boolean hasFocus) {
		super.onWindowFocusChanged(hasFocus);
		if(hasFocus)
			hideSystemBars();
	}

	/**
	 * Modo imersivo: esconde as barras de status e de navegacao (que em
	 * paisagem ficam na lateral, por cima do jogo). Deslizar a partir da borda
	 * as mostra por alguns segundos, sem tirar o jogo da tela cheia.
	 */
	private void hideSystemBars() {
		Window window = getWindow();
		window.setDecorFitsSystemWindows(false);
		WindowInsetsController controller = window.getInsetsController();
		if(controller != null) {
			controller.hide(WindowInsets.Type.systemBars());
			controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
		}
	}

	@Override
	protected void onResume() {
		super.onResume();
		if(waitingForAllFilesAccess) {
			waitingForAllFilesAccess = false;
			if(Environment.isExternalStorageManager()) {
				launchFolderPicker();
			} else {
				showFolderDialog("Sem a permissao \"Acesso a todos os arquivos\" o jogo nao consegue ler a pasta do Victoria 2.");
			}
		}
	}

	/** Chamado pelo C++ quando nao ha pasta do jogo valida configurada. */
	public void requestGameFolder(final String reason) {
		runOnUiThread(() -> showFolderDialog(reason));
	}

	/**
	 * Janela de carregamento, pedida pelo C++ (change_cursor "ocupado") antes
	 * de trabalhos que travam a thread do jogo -- ex.: criar a interface ao
	 * entrar na partida. Ela e desenhada pelo sistema, entao aparece e se anima
	 * mesmo com o jogo travado, e enquanto esta aberta os toques vao para ela:
	 * sem isso o Android mostrava "o app nao esta respondendo". So aparece se o
	 * trabalho passar de 300 ms, para carregamentos rapidos nao piscarem.
	 * Tambem mostra as etapas da abertura do app (extrair arquivos, montar e
	 * carregar o cenario): com ela aberta, uma mensagem nova so troca o texto.
	 * Mensagem vazia fecha.
	 */
	public void setLoading(final String message) {
		runOnUiThread(() -> {
			if(message == null || message.isEmpty()) {
				loadingMessage = null;
				if(pendingLoadingDialog != null) {
					uiHandler.removeCallbacks(pendingLoadingDialog);
					pendingLoadingDialog = null;
				}
				if(loadingDialog != null) {
					loadingDialog.dismiss();
					loadingDialog = null;
					loadingText = null;
				}
				return;
			}
			// aberta ou para abrir: so troca o texto (etapas do carregamento)
			loadingMessage = message;
			if(loadingDialog != null) {
				loadingText.setText(message);
				return;
			}
			if(pendingLoadingDialog != null)
				return;
			pendingLoadingDialog = () -> {
				pendingLoadingDialog = null;
				showLoadingDialog(loadingMessage);
			};
			uiHandler.postDelayed(pendingLoadingDialog, 300);
		});
	}

	private void showLoadingDialog(String message) {
		if(isFinishing() || message == null)
			return;
		int padding = (int)(24 * getResources().getDisplayMetrics().density);
		LinearLayout layout = new LinearLayout(this);
		layout.setOrientation(LinearLayout.HORIZONTAL);
		layout.setGravity(Gravity.CENTER_VERTICAL);
		layout.setPadding(padding, padding, padding, padding);
		ProgressBar spinner = new ProgressBar(this);
		spinner.setIndeterminate(true);
		layout.addView(spinner);
		loadingText = new TextView(this);
		loadingText.setText(message);
		loadingText.setTextSize(18);
		loadingText.setPadding(padding, 0, 0, 0);
		layout.addView(loadingText);
		loadingDialog = new AlertDialog.Builder(this)
			.setView(layout)
			.setCancelable(false)
			.create();
		loadingDialog.setCanceledOnTouchOutside(false);
		showWithoutSystemBars(loadingDialog);
	}

	// Uma janela de dialogo nova traz as barras do sistema de volta. Aberta sem
	// foco, ja com as barras escondidas, e so depois recebe o foco.
	private void showWithoutSystemBars(AlertDialog dialog) {
		Window window = dialog.getWindow();
		if(window != null)
			window.setFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE, WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
		dialog.show();
		if(window != null) {
			WindowInsetsController controller = window.getInsetsController();
			if(controller != null) {
				controller.hide(WindowInsets.Type.systemBars());
				controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
			}
			window.clearFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
		}
	}

	/**
	 * Contador de desempenho (ligado com toque de tres dedos), atualizado pela
	 * thread nativa uma vez por segundo. Fica numa janela propria por cima do
	 * jogo, porque a NativeActivity desenha direto na superficie e nao mostra
	 * views comuns. Nao recebe toques. Texto vazio fecha.
	 */
	public void setStats(final String text) {
		runOnUiThread(() -> {
			if(text == null || text.isEmpty()) {
				if(statsPopup != null) {
					statsPopup.dismiss();
					statsPopup = null;
					statsText = null;
				}
				return;
			}
			if(statsPopup == null) {
				if(isFinishing())
					return;
				int padding = (int)(4 * getResources().getDisplayMetrics().density);
				statsText = new TextView(this);
				statsText.setTextColor(0xFFFFFFFF);
				statsText.setBackgroundColor(0xA0000000);
				statsText.setTypeface(Typeface.MONOSPACE);
				statsText.setTextSize(12);
				statsText.setPadding(padding, padding, padding, padding);
				statsPopup = new PopupWindow(statsText, ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT, false);
				statsPopup.setTouchable(false);
				statsText.setText(text);
				statsPopup.showAtLocation(getWindow().getDecorView(), Gravity.TOP | Gravity.END, padding, padding);
				return;
			}
			statsText.setText(text);
		});
	}

	/**
	 * Campo de texto do jogo com foco: a NativeActivity nao recebe os
	 * caracteres do teclado virtual, entao o texto e digitado aqui e enviado
	 * ao C++, que substitui o conteudo do campo. Cancelar devolve nulo.
	 */
	public void requestText(final String current) {
		runOnUiThread(() -> {
			if(isFinishing()) {
				nativeOnTextResult(null);
				return;
			}
			int padding = (int)(20 * getResources().getDisplayMetrics().density);
			EditText input = new EditText(this);
			input.setSingleLine(true);
			input.setText(current);
			input.selectAll();
			LinearLayout layout = new LinearLayout(this);
			layout.setPadding(padding, padding / 2, padding, 0);
			layout.addView(input, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
			final boolean[] answered = { false };
			AlertDialog dialog = new AlertDialog.Builder(this)
				.setTitle("Digitar texto")
				.setView(layout)
				.setPositiveButton("OK", (d, which) -> {
					answered[0] = true;
					nativeOnTextResult(input.getText().toString());
				})
				.setNegativeButton("Cancelar", null)
				.create();
			dialog.setOnDismissListener(d -> {
				if(!answered[0])
					nativeOnTextResult(null);
			});
			Window window = dialog.getWindow();
			if(window != null)
				window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);
			showWithoutSystemBars(dialog);
			input.requestFocus();
		});
	}

	/**
	 * Chamado pelo C++ ao abrir o jogo quando ha um salvamento automatico
	 * (feito ao sair do app no meio da partida).
	 */
	public void offerContinue(final String unused) {
		runOnUiThread(() -> {
			if(isFinishing())
				return;
			AlertDialog dialog = new AlertDialog.Builder(this)
				.setTitle("Continuar a partida?")
				.setMessage("Há uma partida salva automaticamente quando você saiu do app.")
				.setPositiveButton("Continuar", (d, which) -> nativeContinueGame())
				.setNegativeButton("Agora não", null)
				.create();
			showWithoutSystemBars(dialog);
		});
	}

	/** Mensagem curta na tela (ex.: "Criando o cenario..."). */
	public void showMessage(final String message) {
		runOnUiThread(() -> Toast.makeText(this, message, Toast.LENGTH_LONG).show());
	}

	private void showFolderDialog(String reason) {
		if(folderDialog != null && folderDialog.isShowing())
			folderDialog.dismiss();
		String message = "O Project Alice precisa dos arquivos do Victoria 2 (com as expansoes).\n\n"
			+ "Copie a pasta do jogo para o aparelho e escolha-a na proxima tela "
			+ "(a pasta que contem \"common\", \"map\" e \"gfx\").";
		if(reason != null && !reason.isEmpty())
			message = reason + "\n\n" + message;
		folderDialog = new AlertDialog.Builder(this)
			.setTitle("Pasta do Victoria 2")
			.setMessage(message)
			.setCancelable(false)
			.setPositiveButton("Escolher pasta", (d, w) -> ensureAccessThenPick())
			.setNegativeButton("Sair", (d, w) -> finish())
			.show();
	}

	private void ensureAccessThenPick() {
		// minSdk 31: o acesso direto (caminhos de arquivo, que o C++ usa) a
		// pastas fora do app exige "Acesso a todos os arquivos"
		if(Environment.isExternalStorageManager()) {
			launchFolderPicker();
			return;
		}
		waitingForAllFilesAccess = true;
		try {
			startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
				Uri.parse("package:" + getPackageName())));
		} catch(Exception e) {
			startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
		}
	}

	private void launchFolderPicker() {
		Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
		try {
			startActivityForResult(intent, REQUEST_PICK_FOLDER);
		} catch(Exception e) {
			showFolderDialog("Nao foi possivel abrir o seletor de pastas deste aparelho.");
		}
	}

	@Override
	protected void onActivityResult(int requestCode, int resultCode, Intent data) {
		super.onActivityResult(requestCode, resultCode, data);
		if(requestCode != REQUEST_PICK_FOLDER)
			return;
		if(resultCode != RESULT_OK || data == null || data.getData() == null) {
			showFolderDialog("Nenhuma pasta foi escolhida.");
			return;
		}
		String path = treeUriToPath(data.getData());
		if(path == null) {
			showFolderDialog("Escolha uma pasta do armazenamento do aparelho ou do cartao SD (nao de um servico na nuvem).");
			return;
		}
		nativeOnGameFolderPicked(path);
	}

	/**
	 * content://com.android.externalstorage.documents/tree/primary%3AGames%2FVictoria%202
	 *   -> /storage/emulated/0/Games/Victoria 2
	 * Cartao SD ("XXXX-XXXX:pasta") -> /storage/XXXX-XXXX/pasta
	 */
	static String treeUriToPath(Uri uri) {
		if(!"com.android.externalstorage.documents".equals(uri.getAuthority()))
			return null;
		String docId;
		try {
			docId = DocumentsContract.getTreeDocumentId(uri);
		} catch(Exception e) {
			return null;
		}
		int colon = docId.indexOf(':');
		if(colon < 0)
			return null;
		String volume = docId.substring(0, colon);
		String relative = docId.substring(colon + 1);
		String base = "primary".equalsIgnoreCase(volume)
			? Environment.getExternalStorageDirectory().getAbsolutePath()
			: "/storage/" + volume;
		return relative.isEmpty() ? base : base + "/" + relative;
	}
}
