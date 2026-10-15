package com.torusxo.trs;

import java.awt.BorderLayout;
import java.awt.Dimension;
import java.awt.EventQueue;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

import javax.swing.JComponent;
import javax.swing.JPanel;
import javax.swing.Timer;

import com.samskivert.swing.GroupLayout;

import com.threerings.config.ConfigReference;
import com.threerings.math.Box;
import com.threerings.opengl.camera.OrbitCameraHandler;
import com.threerings.opengl.model.config.ModelConfig;
import com.threerings.opengl.model.tools.ModelViewer;

/**
 * Minimal SpiralView host used by ThreeRings Studio.
 *
 * The OpenGL AWT canvas still needs a native window, but that window contains only the canvas,
 * starts outside the desktop, and is composited into the Qt client. A small control file lets the
 * Qt model list replace the displayed model without restarting the JVM.
 */
public final class TrsModelPreview extends ModelViewer
{
  public TrsModelPreview (String model, Path controlFile)
  {
    super(model);
    _currentModel = model;
    _controlFile = controlFile;

    _frame.setJMenuBar(null);
    _frame.setUndecorated(true);
    _frame.setType(java.awt.Window.Type.UTILITY);
    _frame.setAutoRequestFocus(false);
    _frame.setFocusableWindowState(false);
    _frame.setTitle("TRS Model Preview");
    _frame.setSize(800, 640);
    _frame.setLocation(-32000, -32000);
  }

  @Override
  protected JComponent createCanvasContainer ()
  {
    // ModelViewer's constructor still initializes its controls against this panel, but it is not
    // added to the frame. This leaves the renderer with a canvas-only client area.
    _cpanel = GroupLayout.makeVStretchBox(5);
    _canvas.setMinimumSize(new Dimension(1, 1));
    JPanel panel = new JPanel(new BorderLayout());
    panel.add(_canvas, BorderLayout.CENTER);
    return panel;
  }

  @Override
  protected void didInit ()
  {
    super.didInit();
    _showEnvironment.setSelected(false);
    ((ModelToolPrefs)_eprefs).updateEnvironment();
    _showGrid.setSelected(false);
    _showBounds.setSelected(false);
    _showCompass.setSelected(false);
    _showStats.setSelected(false);
    _grid.getColor().set(0.21F, 0.21F, 0.21F, 0.0F);
    _compositor.getDefaultBackgroundColor().set(0.20F, 0.20F, 0.20F, 0.0F);
    scheduleFit();
    startControlWatcher();
    System.out.println("TRS_PREVIEW_READY: canvas-only background renderer");
  }

  @Override
  protected void willShutdown ()
  {
    _running = false;
    super.willShutdown();
  }

  protected void startControlWatcher ()
  {
    if (_controlFile == null) {
      return;
    }
    Thread watcher = new Thread(() -> {
      while (_running) {
        try {
          if (Files.isRegularFile(_controlFile)) {
            String model = Files.readString(_controlFile, StandardCharsets.UTF_8).trim();
            if (!model.isEmpty() && !model.equals(_currentModel)) {
              _currentModel = model;
              EventQueue.invokeLater(() -> loadModel(model));
            }
          }
          Thread.sleep(100L);
        } catch (InterruptedException e) {
          Thread.currentThread().interrupt();
          return;
        } catch (Exception e) {
          System.err.println("TRS_PREVIEW_CONTROL_ERROR: " + e);
        }
      }
    }, "trs-model-preview-control");
    watcher.setDaemon(true);
    watcher.start();
  }

  protected void loadModel (String model)
  {
    String resourcePath = _rsrcmgr.getResourcePath(new File(model));
    if (resourcePath == null) {
      System.err.println("TRS_PREVIEW_MODEL_ERROR: outside resource directory: " + model);
      return;
    }
    ModelConfig previous = _model.getConfig();
    if (previous != null) {
      previous.removeListener(this);
    }
    ModelConfig.Derived impl = new ModelConfig.Derived();
    impl.model = new ConfigReference<ModelConfig>(resourcePath);
    ModelConfig config = new ModelConfig(impl);
    config.init(_cfgmgr);
    config.addListener(this);
    _model.setConfig(config);
    _model.reset();
    scheduleFit();
    System.out.println("TRS_PREVIEW_MODEL: " + resourcePath);
  }

  protected void scheduleFit ()
  {
    for (int delay : new int[] { 100, 400, 1000 }) {
      Timer timer = new Timer(delay, event -> fitModel());
      timer.setRepeats(false);
      timer.start();
    }
  }

  protected void fitModel ()
  {
    if (_model == null) {
      return;
    }
    _model.updateBounds();
    Box bounds = _model.getBounds();
    if (bounds == null || bounds.isEmpty()) {
      return;
    }
    OrbitCameraHandler camera = (OrbitCameraHandler)_camhand;
    camera.getTarget().set(bounds.getCenter());
    float longest = Math.max(0.1F, bounds.getLongestEdge());
    float aspect = Math.max(0.25F, (float)_canvas.getWidth() / Math.max(1, _canvas.getHeight()));
    camera.getCoords().distance = Math.max(2F, Math.min(50F,
      longest * (aspect < 1F ? 2.4F / aspect : 2.4F)));
  }

  public static void main (String[] args)
  {
    if (args.length < 1) {
      throw new IllegalArgumentException("Expected model path and optional control-file path");
    }
    Path control = args.length > 1 ? Path.of(args[1]) : null;
    new TrsModelPreview(args[0], control).startup();
  }

  protected final Path _controlFile;
  protected volatile String _currentModel;
  protected volatile boolean _running = true;
}
