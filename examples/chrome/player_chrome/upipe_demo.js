/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Xavier Boulet
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

// This function is called by common.js when the NaCl module is
// loaded.

function moduleDidLoad() {
  // Once we load, hide the plugin. In this example, we don't display anything
  // in the plugin, so it is fine to hide it.

  // After the NaCl module has loaded, common.naclModule is a reference to the
  // NaCl module's <embed> element.
  //
  // postMessage sends a message to it.
}

function run(mode) {
  var multicastAddr = document.getElementById('multicastAddr').value;
  var multicastPort = Number(document.getElementById('multicastPort').value);
  var sourceAddr = document.getElementById('sourceAddr').value;
  var relayAddr = document.getElementById('relayAddr').value;
  common.naclModule.postMessage({
    'message': 'set_uri',
    'mode': mode,
    'value': sourceAddr + '@' + multicastAddr + ':' + multicastPort,
    'relay': relayAddr
  });
}

// Add event listeners after the NaCl module has loaded.  These listeners will
// forward messages to the NaCl module via postMessage()
function attachListeners() {
  document.getElementById('udp').addEventListener('click', function () {
    run("udp");
  });
  document.getElementById('ssm').addEventListener('click', function () {
    run("ssm");
  });
  document.getElementById('amt').addEventListener('click', function () {
    run("amt");
  });
  document.getElementById('any').addEventListener('click', function () {
    run("any");
  });
  document.getElementById('stop').addEventListener('click', function () {
    common.naclModule.postMessage({
      'message': 'stop'
    });
  });
  document.getElementById('quit').addEventListener('click', function () {
    common.naclModule.postMessage({
      'message': 'quit'
    });
  });
}

first_message = [1,1,1,1,1,1];
graph2 = [null,null];
chan_par_pipe = [0,0,0,0,0,0];


function handleMessage(message) {
  if (message.data.indexOf("error:") == 0) {
    var error = message.data.substring(6);
    document.getElementById('statusField').innerHTML = "ERROR " + error + ". Please check that your browser supports opening NaCl sockets.";
  }
}

function createCanvas(n) {
      var div = document.querySelectorAll("#listener");
      div = div[0];
      var canvas = document.createElement('canvas');
      div.appendChild(canvas);
      if (typeof G_vmlCanvasManager != 'undefined') {
        canvas = G_vmlCanvasManager.initElement(canvas);
      }  
      var ctx = canvas.getContext("2d");
      return ctx;
    }


function BarGraph(ctx) {

  // Private properties and methods
  
  var that = this;
  var startArr;
  var startLoud_max;
  var endArr;
  var looping = false;
    
  // Loop method adjusts the height of bar and redraws if neccessary
  var loop = function () {

    var delta;
    var animationComplete = true;

    // Boolean to prevent update function from looping if already looping
    looping = true;
    
    // For each bar
    for (var i = 0; i < endArr.length; i += 1) {
    // Change the current bar height toward its target height
    delta = (endArr[i] - startArr[i]) / that.animationSteps;
    delta_l = (endLoud_max[i]-startLoud_max[i]) / that.animationSteps;
    that.curArr[i] += delta;
    that.curLoud_max[i] += delta_l;
    // If any change is made then flip a switch
    if (delta) {
      animationComplete = false;
    }
    }
    // If no change was made to any bars then we are done
    if (animationComplete) {
    looping = false;
    } else {
    // Draw and call loop again
    //console.log("curloud_max = "+that.curLoud_max[0]);
    draw(that.curArr,that.curLoud_max);
    setTimeout(loop, that.animationInterval / that.animationSteps);
    }
  };
    
  // Draw method updates the canvas with the current display
  var draw = function (arr,loud_max) {
              
    var barWidth;
    var barHeight;
    var border = 2;
    var ratio;
    var maxBarHeight;
    var gradient;
    var largestValue;
    var graphAreaX = 0;
    var graphAreaY = 0;
    var graphAreaWidth = that.width;
    var graphAreaHeight = that.height;
    var i;
    
    // Update the dimensions of the canvas only if they have changed
    if (ctx.canvas.width !== that.width || ctx.canvas.height !== that.height) {
    ctx.canvas.width = that.width;
    ctx.canvas.height = that.height;
    }
        
    // Draw the background color
    ctx.fillStyle = that.backgroundColor;
    ctx.fillRect(0, 0, that.width, that.height);
          
    // If x axis labels exist then make room  
    if (that.xAxisLabelArr.length) {
    graphAreaHeight -= 40;
    }
        
    // Calculate dimensions of the bar
    barWidth = 50 - that.margin * 2;
    maxBarHeight = graphAreaHeight - 25;
        
    // Determine the largest value in the bar array
    var largestValue = 120;
    
    // For each bar
    for (i = 0; i < arr.length; i += 1) {
    // Set the ratio of current bar compared to the maximum
    if (that.maxValue) {
      ratio = arr[i] / that.maxValue;
    } else {
      ratio = arr[i] / largestValue;
    }
    barHeight = ratio * maxBarHeight;    
    // Draw bar color if it is large enough to be visible
    if (barHeight > border * 2) {    
      ctx.fillStyle=that.colors[i % that.colors.length];
      ctx.fillRect(that.margin + i * 50 + border,
        graphAreaHeight - barHeight + border,
        barWidth - border * 2,
        barHeight - border * 2);  
    b = maxBarHeight * ((loud_max[i]+100) / largestValue);
    var ymax = graphAreaHeight - b + border;
    ctx.fillStyle="#FF0000";
    ctx.fillRect(that.margin + i * 50 + border,ymax,barWidth - border * 2,2);
    }

    // Write bar value
    ctx.fillStyle = "#333";
    ctx.font = "bold 12px sans-serif";
    ctx.textAlign = "center";

      ctx.fillText(parseInt(arr[i]-100,10),
      i * 50 + 50/ 2,
      graphAreaHeight - barHeight - 10);

    // Draw bar label if it exists
    if (that.xAxisLabelArr[i]) {          
      // Use try / catch to stop IE 8 from going to error town        
      ctx.fillStyle = "#333";
      ctx.font = "bold 12px sans-serif";
      ctx.textAlign = "center";
      try{
      ctx.fillText(that.xAxisLabelArr[i],
        i * 50 + 50 / 2,
        that.height - 10);
      } catch (ex) {}
      }
    }
    };

  // Public properties and methods
  
  this.width = 300;
  this.height = 150;  
  this.maxValue;
  this.margin = 5;
  this.colors = ["green", "blue"];
  this.curArr = [];
  this.curLoud_max = [];
  this.backgroundColor = "#fff";
  this.xAxisLabelArr = [];
  this.yAxisLabelArr = [];
  this.animationInterval = 100;
  this.animationSteps = 10;
  
  // Update method sets the end bar array and starts the animation
  this.update = function (newArr,newLoud_max) {
    // If length of target and current array is different 
    if (that.curArr.length !== newArr.length) {
    that.curArr = newArr;
    that.curLoud_max = newLoud_max;
    draw(newArr,newLoud_max);
    } else {
    // Set the starting array to the current array
    startArr = that.curArr;
    startLoud_max = that.curLoud_max;
    // Set the target array to the new array
    endArr = newArr;
    endLoud_max = newLoud_max;
    // Animate from the start array to the end array
    if (!looping) {  
      loop();
    }
    }
  }; 
}
